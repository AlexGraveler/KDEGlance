// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "mockdolphin.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>

KFileItemModel::KFileItemModel(QObject *parent)
    : QObject(parent)
{
}

int KFileItemModel::count() const
{
    return int(m_items.size());
}

KFileItem KFileItemModel::fileItem(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items.at(index) : KFileItem();
}

int KFileItemModel::index(const QUrl &url) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).url() == url) {
            return i;
        }
    }
    return -1;
}

void KFileItemModel::setItems(const KFileItemList &items)
{
    m_items = items;
    Q_EMIT directoryLoadingCompleted();
}

void KFileItemModel::insertItem(int index, const KFileItem &item)
{
    m_items.insert(index, item);
    KItemRangeList ranges;
    ranges.append(KItemRange{index, 1});
    Q_EMIT itemsInserted(ranges);
}

void KFileItemModel::removeItem(int index)
{
    m_items.removeAt(index);
    KItemRangeList ranges;
    ranges.append(KItemRange{index, 1});
    Q_EMIT itemsRemoved(ranges);
}

KItemListSelectionManager::KItemListSelectionManager(QObject *parent)
    : QObject(parent)
{
}

int KItemListSelectionManager::currentItem() const
{
    return m_current;
}

bool KItemListSelectionManager::isSelected(int index) const
{
    return m_selection.contains(index);
}

bool KItemListSelectionManager::hasSelection() const
{
    return !m_selection.isEmpty();
}

void KItemListSelectionManager::setCurrentItem(int current)
{
    m_current = current;
    ++changes;
}

void KItemListSelectionManager::clearSelection()
{
    m_selection.clear();
    ++changes;
}

void KItemListSelectionManager::setSelected(int index, int count, SelectionMode mode)
{
    for (int i = index; i < index + count; ++i) {
        if (mode == Select) {
            m_selection.insert(i);
        } else if (mode == Deselect) {
            m_selection.remove(i);
        } else if (!m_selection.remove(i)) {
            m_selection.insert(i);
        }
    }
    ++changes;
}

void KItemListSelectionManager::beginAnchoredSelection(int anchor)
{
    m_anchor = anchor;
}

void KItemListSelectionManager::endAnchoredSelection()
{
    m_anchor = -1;
}

QSet<int> KItemListSelectionManager::selection() const
{
    return m_selection;
}

int KItemListSelectionManager::anchor() const
{
    return m_anchor;
}

KItemListController::KItemListController(QObject *parent)
    : QObject(parent)
{
}

bool KItemListController::isSearchAsYouTypeActive() const
{
    return typeAheadActive;
}

bool KItemListController::selectionMode() const
{
    return selectionModeActive;
}

void KItemListView::scrollToItem(int index, ViewItemPosition viewItemPosition)
{
    lastScrolled = index;
    lastPosition = viewItemPosition;
}

KItemListContainer::KItemListContainer(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_controller(new KItemListController(this)) // Dolphin: controller->setParent(container)
    , m_selection(new KItemListSelectionManager(m_controller))
    , m_view(new KItemListView)
{
    auto *graphicsView = new QGraphicsView(new QGraphicsScene(this), this);
    setViewport(graphicsView);
    graphicsView->scene()->addItem(m_view);
    setFocusPolicy(Qt::StrongFocus);
}

KItemListController *KItemListContainer::controller() const
{
    return m_controller;
}

KItemListSelectionManager *KItemListContainer::selectionManager() const
{
    return m_selection;
}

KItemListView *KItemListContainer::listView() const
{
    return m_view;
}

void KItemListContainer::keyPressEvent(QKeyEvent *event)
{
    keysReceived.append(event->key());
    event->accept();
}

DolphinView::DolphinView(QWidget *parent)
    : QWidget(parent)
    , m_model(new KFileItemModel(this)) // Dolphin: m_model = new KFileItemModel(this)
    , m_container(new KItemListContainer(this))
{
    m_model->setParent(m_container->controller()); // Actual Dolphin 26.08 ownership.
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_container);
}

KFileItemModel *DolphinView::model() const
{
    return m_model;
}

KItemListContainer *DolphinView::container() const
{
    return m_container;
}

void DolphinView::selectOnly(int index)
{
    KItemListSelectionManager *s = m_container->selectionManager();
    s->clearSelection();
    s->setCurrentItem(index);
    if (index >= 0) {
        s->setSelected(index);
    }
    KFileItemList selection;
    if (index >= 0) {
        selection << m_model->fileItem(index);
    }
    Q_EMIT selectionChanged(selection);
}

DolphinMainWindow::DolphinMainWindow(int views)
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    for (int i = 0; i < views; ++i) {
        auto *view = new DolphinView(central);
        layout->addWidget(view);
        m_views << view;
    }
    setCentralWidget(central);
    resize(800, 500);
}

DolphinView *DolphinMainWindow::view(int i) const
{
    return m_views.value(i);
}
