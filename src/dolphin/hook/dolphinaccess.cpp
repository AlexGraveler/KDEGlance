// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "dolphinaccess.h"

#include <KFileItem>

#include <QAbstractScrollArea>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

#include <dlfcn.h>

Q_LOGGING_CATEGORY(lcAccess, "kdeglance.dolphin.access", QtInfoMsg)

namespace KDEGlance
{

// Member functions are called through plain function pointers whose first
// parameter is `this`. With the Itanium C++ ABI (x86-64, AArch64, ...) this is
// exactly how non-virtual member calls are made, including the hidden return
// slot for class types such as KFileItem.
struct DolphinAccess::Api {
    using CountFn = int (*)(const void *);
    using FileItemFn = KFileItem (*)(const void *, int);
    using IndexOfUrlFn = int (*)(const void *, const QUrl &);
    using IntConstFn = int (*)(const void *);
    using IsSelectedFn = bool (*)(const void *, int);
    using BoolConstFn = bool (*)(const void *);
    using SetIntFn = void (*)(void *, int);
    using VoidFn = void (*)(void *);
    using SetSelectedFn = void (*)(void *, int, int, int);
    using ScrollPosFn = void (*)(void *, int, int);

    CountFn modelCount = nullptr;
    FileItemFn modelFileItem = nullptr;
    IndexOfUrlFn modelIndexOfUrl = nullptr;
    IntConstFn selCurrentItem = nullptr;
    IsSelectedFn selIsSelected = nullptr;
    BoolConstFn selHasSelection = nullptr;
    SetIntFn selSetCurrentItem = nullptr;
    VoidFn selClearSelection = nullptr;
    SetSelectedFn selSetSelected = nullptr;
    SetIntFn selBeginAnchored = nullptr;
    VoidFn selEndAnchored = nullptr;
    ScrollPosFn viewScrollToItemPos = nullptr; // scrollToItem(int, ViewItemPosition)
    SetIntFn viewScrollToItem = nullptr; // older scrollToItem(int)
    BoolConstFn ctrlTypeAhead = nullptr;
    BoolConstFn ctrlSelectionMode = nullptr;
};

namespace
{
// KItemListSelectionManager::SelectionMode::Select
constexpr int SelectModeSelect = 0;
// KItemListView::ViewItemPosition::Nearest
constexpr int ViewItemPositionNearest = 3;

template<typename T>
T lookup(const char *symbol, QStringList *missing)
{
    void *p = ::dlsym(RTLD_DEFAULT, symbol);
    if (!p) {
        missing->append(QString::fromLatin1(symbol));
    }
    return reinterpret_cast<T>(p);
}

QObject *childInheriting(const QObject *parent, const char *className)
{
    if (!parent) {
        return nullptr;
    }
    const auto children = parent->children();
    for (QObject *child : children) {
        if (child->inherits(className)) {
            return child;
        }
    }
    return nullptr;
}
} // namespace

const DolphinAccess &DolphinAccess::instance()
{
    static const DolphinAccess access;
    return access;
}

DolphinAccess::DolphinAccess()
    : m_api(new Api)
{
    resolve();
}

void DolphinAccess::resolve()
{
    Api &a = *m_api;
    a.modelCount = lookup<Api::CountFn>("_ZNK14KFileItemModel5countEv", &m_missing);
    a.modelFileItem = lookup<Api::FileItemFn>("_ZNK14KFileItemModel8fileItemEi", &m_missing);
    a.modelIndexOfUrl = lookup<Api::IndexOfUrlFn>("_ZNK14KFileItemModel5indexERK4QUrl", &m_missing);
    a.selCurrentItem = lookup<Api::IntConstFn>("_ZNK25KItemListSelectionManager11currentItemEv", &m_missing);
    a.selIsSelected = lookup<Api::IsSelectedFn>("_ZNK25KItemListSelectionManager10isSelectedEi", &m_missing);
    a.selHasSelection = lookup<Api::BoolConstFn>("_ZNK25KItemListSelectionManager12hasSelectionEv", &m_missing);
    a.selSetCurrentItem = lookup<Api::SetIntFn>("_ZN25KItemListSelectionManager14setCurrentItemEi", &m_missing);
    a.selClearSelection = lookup<Api::VoidFn>("_ZN25KItemListSelectionManager14clearSelectionEv", &m_missing);
    a.selSetSelected = lookup<Api::SetSelectedFn>("_ZN25KItemListSelectionManager11setSelectedEiiNS_13SelectionModeE", &m_missing);
    a.selBeginAnchored = lookup<Api::SetIntFn>("_ZN25KItemListSelectionManager22beginAnchoredSelectionEi", &m_missing);
    a.selEndAnchored = lookup<Api::VoidFn>("_ZN25KItemListSelectionManager20endAnchoredSelectionEv", &m_missing);

    // Optional ones: features degrade gracefully without them.
    QStringList optionalMissing;
    a.viewScrollToItemPos = lookup<Api::ScrollPosFn>("_ZN13KItemListView12scrollToItemEiNS_16ViewItemPositionE", &optionalMissing);
    a.viewScrollToItem = lookup<Api::SetIntFn>("_ZN13KItemListView12scrollToItemEi", &optionalMissing);
    a.ctrlTypeAhead = lookup<Api::BoolConstFn>("_ZNK19KItemListController23isSearchAsYouTypeActiveEv", &optionalMissing);
    a.ctrlSelectionMode = lookup<Api::BoolConstFn>("_ZNK19KItemListController13selectionModeEv", &optionalMissing);

    if (!m_missing.isEmpty()) {
        qCWarning(lcAccess) << "Dolphin internals not found, using fallbacks:" << m_missing;
    }
    if (!optionalMissing.isEmpty()) {
        qCInfo(lcAccess) << "optional Dolphin internals not found:" << optionalMissing;
    }
}

bool DolphinAccess::hasModelAccess() const
{
    return m_api->modelCount && m_api->modelFileItem && m_api->selCurrentItem && m_api->selIsSelected && m_api->selHasSelection;
}

bool DolphinAccess::hasSelectionAccess() const
{
    return hasModelAccess() && m_api->modelIndexOfUrl && m_api->selSetCurrentItem && m_api->selClearSelection && m_api->selSetSelected
        && m_api->selBeginAnchored && m_api->selEndAnchored;
}

QStringList DolphinAccess::missingSymbols() const
{
    return m_missing;
}

QWidget *DolphinAccess::viewFor(QWidget *widget)
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (w->inherits("DolphinView")) {
            return w;
        }
        if (w->isWindow()) {
            break;
        }
    }
    return nullptr;
}

QWidget *DolphinAccess::containerOf(QWidget *view)
{
    return qobject_cast<QWidget *>(childInheriting(view, "KItemListContainer"));
}

QObject *DolphinAccess::controllerOf(QWidget *view)
{
    return childInheriting(containerOf(view), "KItemListController");
}

QObject *DolphinAccess::selectionManagerOf(QWidget *view)
{
    return childInheriting(controllerOf(view), "KItemListSelectionManager");
}

QObject *DolphinAccess::modelOf(QWidget *view)
{
    // KItemListController reparents its model in current Dolphin releases.
    // Older releases left it under DolphinView. Stay within this view's
    // controller so split panes never pick up each other's model.
    if (auto *model = childInheriting(controllerOf(view), "KFileItemModel")) {
        return model;
    }
    return childInheriting(view, "KFileItemModel");
}

QObject *DolphinAccess::itemListViewOf(QWidget *view)
{
    if (auto *listView = childInheriting(controllerOf(view), "KItemListView")) {
        return listView;
    }
    auto *container = qobject_cast<QAbstractScrollArea *>(containerOf(view));
    if (!container) {
        return nullptr;
    }
    auto *graphicsView = qobject_cast<QGraphicsView *>(container->viewport());
    if (!graphicsView || !graphicsView->scene()) {
        return nullptr;
    }
    const auto items = graphicsView->scene()->items();
    for (QGraphicsItem *item : items) {
        if (QGraphicsObject *object = item->toGraphicsObject(); object && object->inherits("KItemListView")) {
            return object;
        }
    }
    return nullptr;
}

bool DolphinAccess::isFileViewFocus(QWidget *focus, QWidget **viewOut)
{
    if (!focus) {
        return false;
    }
    // Text input anywhere (inline rename, filter bar, location bar) keeps Space.
    if (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) || qobject_cast<QPlainTextEdit *>(focus)) {
        return false;
    }
    QWidget *view = viewFor(focus);
    if (!view) {
        return false;
    }
    QWidget *container = containerOf(view);
    if (!container || !(focus == container || container->isAncestorOf(focus))) {
        return false;
    }
    if (viewOut) {
        *viewOut = view;
    }
    return true;
}

int DolphinAccess::targetIndex(const QObject *model, const QObject *selection, const std::optional<QUrl> &preferred) const
{
    const Api &a = *m_api;
    const int count = a.modelCount(model);
    if (count <= 0) {
        return -1;
    }
    int target = -1;
    if (preferred) {
        if (a.modelIndexOfUrl) {
            target = a.modelIndexOfUrl(model, *preferred);
        }
        if (target < 0) {
            // The context menu may report the most-local URL; compare both.
            for (int i = 0; i < count && target < 0; ++i) {
                const KFileItem item = a.modelFileItem(model, i);
                if (item.url() == *preferred || item.mostLocalUrl() == *preferred) {
                    target = i;
                }
            }
        }
    } else {
        const int current = a.selCurrentItem(selection);
        const bool hasSelection = a.selHasSelection(selection);
        if (current >= 0 && current < count && (!hasSelection || a.selIsSelected(selection, current))) {
            target = current;
        } else if (hasSelection) {
            for (int i = 0; i < count; ++i) {
                if (a.selIsSelected(selection, i)) {
                    target = i;
                    break;
                }
            }
        }
    }
    if (target < 0 || target >= count) {
        return -1;
    }
    const KFileItem item = a.modelFileItem(model, target);
    if (item.isNull() || item.isDir()) {
        return -1; // folders keep Dolphin's own Space behaviour
    }
    return target;
}

DolphinAccess::Snapshot DolphinAccess::collect(const QObject *model, int target) const
{
    const Api &a = *m_api;
    const int count = a.modelCount(model);
    Snapshot s;
    s.urls.reserve(count);
    s.modelUrls.reserve(count);
    for (int i = 0; i < count; ++i) {
        const KFileItem item = a.modelFileItem(model, i);
        if (item.isNull() || item.isDir()) {
            continue;
        }
        if (i == target) {
            s.current = int(s.urls.size());
        }
        s.urls.append(item.mostLocalUrl());
        s.modelUrls.append(item.url());
    }
    return s;
}

std::optional<DolphinAccess::Snapshot> DolphinAccess::snapshot(QWidget *view, const std::optional<QUrl> &preferred) const
{
    if (!hasModelAccess() || !view) {
        return std::nullopt;
    }
    const QObject *model = modelOf(view);
    const QObject *selection = selectionManagerOf(view);
    if (!model || !selection) {
        return std::nullopt;
    }
    const int target = targetIndex(model, selection, preferred);
    if (target < 0) {
        return std::nullopt;
    }
    Snapshot s = collect(model, target);
    if (s.current < 0) {
        return std::nullopt;
    }
    return s;
}

bool DolphinAccess::hasFileTarget(QWidget *view) const
{
    if (!hasModelAccess() || !view) {
        return false;
    }
    const QObject *model = modelOf(view);
    const QObject *selection = selectionManagerOf(view);
    return model && selection && targetIndex(model, selection, std::nullopt) >= 0;
}

std::optional<DolphinAccess::Snapshot> DolphinAccess::files(QWidget *view) const
{
    if (!hasModelAccess() || !view) {
        return std::nullopt;
    }
    const QObject *model = modelOf(view);
    if (!model) {
        return std::nullopt;
    }
    return collect(model, -1);
}

bool DolphinAccess::isTypeAheadActive(QWidget *view, bool *known) const
{
    const QObject *controller = controllerOf(view);
    if (!m_api->ctrlTypeAhead || !controller) {
        *known = false;
        return false;
    }
    *known = true;
    return m_api->ctrlTypeAhead(controller);
}

bool DolphinAccess::isSelectionModeActive(QWidget *view) const
{
    const QObject *controller = controllerOf(view);
    if (!m_api->ctrlSelectionMode || !controller) {
        return false;
    }
    return m_api->ctrlSelectionMode(controller);
}

bool DolphinAccess::selectUrl(QWidget *view, const QUrl &modelUrl) const
{
    if (!hasSelectionAccess() || !view) {
        return false;
    }
    QObject *model = modelOf(view);
    QObject *selection = selectionManagerOf(view);
    if (!model || !selection) {
        return false;
    }
    const Api &a = *m_api;
    const int index = a.modelIndexOfUrl(model, modelUrl);
    if (index < 0 || index >= a.modelCount(model)) {
        return false;
    }
    a.selEndAnchored(selection);
    a.selClearSelection(selection);
    a.selSetCurrentItem(selection, index);
    a.selSetSelected(selection, index, 1, SelectModeSelect);
    // Shift+click/arrow in Dolphin later extends from here.
    a.selBeginAnchored(selection, index);

    if (QObject *listView = itemListViewOf(view)) {
        if (a.viewScrollToItemPos) {
            a.viewScrollToItemPos(listView, index, ViewItemPositionNearest);
        } else if (a.viewScrollToItem) {
            a.viewScrollToItem(listView, index);
        }
    }
    return true;
}

} // namespace KDEGlance
