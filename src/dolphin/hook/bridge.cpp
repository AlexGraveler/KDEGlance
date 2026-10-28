// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "bridge.h"

#include "constants.h"
#include "dolphinaccess.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QScreen>
#include <QWidget>
#include <QWindow>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcBridge, "kdeglance.dolphin", QtInfoMsg)

namespace KDEGlance
{

namespace
{
QPointer<Bridge> s_bridge;
constexpr int ListDebounceMs = 150;
constexpr int TypeAheadFallbackMs = 1000; // Dolphin's type-ahead timeout
} // namespace

Bridge *Bridge::ensure()
{
    if (s_bridge) {
        return s_bridge;
    }
    auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
    // Only Dolphin itself (not Konqueror's embedded view, not file dialogs).
    if (!app || QCoreApplication::applicationName() != u"dolphin"_s) {
        return nullptr;
    }
    // Emergency switch: KDEGLANCE_DISABLE_DOLPHIN_HOOK=1 dolphin
    if (qEnvironmentVariableIntValue("KDEGLANCE_DISABLE_DOLPHIN_HOOK") != 0) {
        qCInfo(lcBridge) << "KDEGlance Dolphin integration disabled by KDEGLANCE_DISABLE_DOLPHIN_HOOK";
        return nullptr;
    }
    s_bridge = new Bridge(app);
    return s_bridge;
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
{
    loadSettings();

    m_listTimer.setSingleShot(true);
    m_listTimer.setInterval(ListDebounceMs);
    connect(&m_listTimer, &QTimer::timeout, this, &Bridge::sendUpdatedList);

    qApp->installEventFilter(this);
    qApp->setProperty(BridgeAppProperty, QVariant::fromValue<QObject *>(this));
    connect(qApp, &QCoreApplication::aboutToQuit, this, &Bridge::shutdown);

    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QString(DBusBridgePath), this, QDBusConnection::ExportScriptableSlots);
    const QString service(DBusService);
    const QString path(DBusViewerPath);
    const QString iface(DBusViewerInterface);
    bus.connect(service, path, iface, u"CurrentChanged"_s, this, SLOT(onViewerCurrentChanged(QString, QString)));
    bus.connect(service, path, iface, u"Closed"_s, this, SLOT(onViewerClosed(QString, QString)));
    bus.connect(service, path, iface, u"SettingsChanged"_s, this, SLOT(onViewerSettingsChanged()));

    const DolphinAccess &access = DolphinAccess::instance();
    if (!access.hasModelAccess()) {
        // Without model access, remember each view's last selection through
        // DolphinView's public selectionChanged(KFileItemList) signal.
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
            if (QWidget *view = DolphinAccess::viewFor(now)) {
                trackFallbackSelection(view);
            }
        });
    }

    if (m_settings.prestartService()) {
        // Starts the viewer through D-Bus activation, without waiting.
        sendToViewer(u"Ping"_s, {});
    }
    qCInfo(lcBridge) << "KDEGlance Dolphin integration active in Dolphin" << QCoreApplication::applicationVersion()
                     << "; model access:" << access.hasModelAccess()
                     << "selection sync:" << access.hasSelectionAccess();
}

Bridge::~Bridge()
{
    shutdown();
}

void Bridge::shutdown()
{
    if (m_down) {
        return;
    }
    m_down = true;
    m_listTimer.stop();
    // Also reached from ~QCoreApplication (if Dolphin exits without
    // aboutToQuit): touch the application and the bus only while they exist.
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->removeEventFilter(this);
        app->setProperty(BridgeAppProperty, QVariant());
        QDBusConnection::sessionBus().unregisterObject(QString(DBusBridgePath));
    }
}

void Bridge::loadSettings()
{
    m_settings.load();
    const QKeySequence seq = m_settings.shortcut();
    m_hotkey = seq.isEmpty() ? QKeyCombination(Qt::Key_Space) : seq[0];
    m_syncSelection = m_settings.syncSelection();
}

void Bridge::onViewerSettingsChanged()
{
    loadSettings();
}

bool Bridge::matchesHotkey(const QKeyEvent *event) const
{
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    return QKeyCombination(mods, Qt::Key(event->key())) == m_hotkey;
}

bool Bridge::canHandleKeyIn(QWidget *view) const
{
    const DolphinAccess &access = DolphinAccess::instance();
    bool known = false;
    bool typeAhead = access.isTypeAheadActive(view, &known);
    if (!known) {
        typeAhead = m_lastTypeAheadKey.isValid() && m_lastTypeAheadKey.elapsed() < TypeAheadFallbackMs;
    }
    if (typeAhead && m_hotkey == QKeyCombination(Qt::Key_Space)) {
        return false; // the user is typing a file name that contains a space
    }
    if (access.isSelectionModeActive(view)) {
        return false; // Space toggles items in Dolphin's selection mode
    }
    if (access.hasModelAccess()) {
        return access.hasFileTarget(view);
    }
    // Fallback: a remembered selection with a file in it.
    const KFileItemList selection = m_fallbackSelection.value(view);
    return std::any_of(selection.cbegin(), selection.cend(), [](const KFileItem &i) {
        return !i.isDir();
    });
}

bool Bridge::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (m_down || (type != QEvent::KeyPress && type != QEvent::ShortcutOverride)) {
        return false;
    }
    QWidget *focus = QApplication::focusWidget();
    if (watched != focus) {
        return false; // only the first delivery, to the focus widget
    }
    auto *ke = static_cast<QKeyEvent *>(event);
    if (!matchesHotkey(ke)) {
        if (type == QEvent::KeyPress && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            const QString text = ke->text();
            if (!text.isEmpty() && text.at(0).isPrint() && !text.at(0).isSpace() && DolphinAccess::isFileViewFocus(focus)) {
                m_lastTypeAheadKey.start();
            }
        }
        return false;
    }
    QWidget *view = nullptr;
    if (!DolphinAccess::isFileViewFocus(focus, &view) || !canHandleKeyIn(view)) {
        return false;
    }
    if (type == QEvent::ShortcutOverride) {
        // Claim the key so no Dolphin shortcut (e.g. selection mode) fires.
        event->accept();
        return true;
    }
    if (!ke->isAutoRepeat()) {
        trigger(view, Origin::Hotkey);
    }
    return true;
}

bool Bridge::openFromContextMenu(const QList<QUrl> &urls, QWidget *window)
{
    if (urls.isEmpty()) {
        return false;
    }
    QWidget *view = nullptr;
    if (window) {
        // The view that had the focus when the menu opened.
        view = DolphinAccess::viewFor(window->focusWidget());
        if (!view) {
            const auto views = window->findChildren<QWidget *>();
            for (QWidget *w : views) {
                if (w->inherits("DolphinView") && w->isVisible()) {
                    view = w;
                    break;
                }
            }
        }
    }
    if (view && trigger(view, Origin::ContextMenu, urls.first())) {
        return true;
    }
    // No usable view: the viewer lists the folder itself.
    m_sessionView.clear();
    m_sessionId = u"%1/menu-%2"_s.arg(QDBusConnection::sessionBus().baseService()).arg(++m_sessionCounter);
    QStringList list;
    for (const QUrl &u : urls) {
        list << u.toString();
    }
    sendToViewer(u"Open"_s, {QStringList{list.first()}, 0, QVariantMap{{u"origin"_s, u"menu"_s}}});
    return true;
}

bool Bridge::trigger(QWidget *view, Origin origin, const std::optional<QUrl> &preferred)
{
    const DolphinAccess &access = DolphinAccess::instance();
    QStringList urls;
    int index = 0;
    m_localToModelUrl.clear();

    if (auto snap = access.snapshot(view, preferred)) {
        urls.reserve(snap->urls.size());
        for (int i = 0; i < snap->urls.size(); ++i) {
            const QString local = snap->urls.at(i).toString();
            urls << local;
            m_localToModelUrl.insert(local, snap->modelUrls.at(i));
        }
        index = snap->current;
    } else if (!access.hasModelAccess()) {
        KFileItemList candidates;
        if (preferred) {
            candidates << KFileItem(*preferred);
        } else {
            candidates = m_fallbackSelection.value(view);
        }
        for (const KFileItem &item : std::as_const(candidates)) {
            if (!item.isDir()) {
                urls << item.mostLocalUrl().toString();
                break;
            }
        }
        if (urls.isEmpty()) {
            return false;
        }
    } else {
        return false;
    }

    // Space again on the file that is being shown closes the preview.
    if (m_sessionView == view && !m_sessionId.isEmpty() && origin == Origin::Hotkey) {
        m_sessionUrls = urls;
        m_viewerCurrent = urls.value(index);
        sendToViewer(u"Toggle"_s, {urls, index, QVariantMap{{u"session"_s, m_sessionId}}});
        return true;
    }

    if (m_sessionView && m_sessionView != view) {
        disconnect(m_sessionView, SIGNAL(selectionChanged(KFileItemList)), this, SLOT(onSessionSelectionChanged(KFileItemList)));
    }
    m_sessionId = u"%1/%2"_s.arg(QDBusConnection::sessionBus().baseService()).arg(++m_sessionCounter);
    m_sessionView = view;
    m_sessionUrls = urls;
    m_viewerCurrent = urls.value(index);
    m_lastSentListKey.clear();
    watchModel(view);
    // If Dolphin keeps the keyboard focus (e.g. the compositor refused to
    // activate the preview), the preview follows Dolphin's selection.
    connect(view, SIGNAL(selectionChanged(KFileItemList)), this, SLOT(onSessionSelectionChanged(KFileItemList)), Qt::UniqueConnection);

    QVariantMap options{
        {u"session"_s, m_sessionId},
        {u"origin"_s, origin == Origin::Hotkey ? u"hotkey"_s : u"menu"_s},
        {u"listComplete"_s, access.hasModelAccess()},
    };
    if (QWindow *w = view->window()->windowHandle(); w && w->screen()) {
        options.insert(u"screen"_s, w->screen()->name());
    }
    sendToViewer(u"Open"_s, {urls, index, options});
    return true;
}

void Bridge::sendToViewer(const QString &method, const QVariantList &args)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBusService), QString(DBusViewerPath), QString(DBusViewerInterface), method);
    msg.setArguments(args);
    // Fire and forget: never block Dolphin. Activation starts the service.
    QDBusConnection::sessionBus().asyncCall(msg, 10000);
}

void Bridge::watchModel(QWidget *view)
{
    if (m_sessionModel) {
        disconnect(m_sessionModel, nullptr, this, nullptr);
    }
    QObject *model = DolphinAccess::modelOf(view);
    m_sessionModel = model;
    if (!model) {
        return;
    }
    // KItemModelBase/KFileItemModel signals; their argument types are private
    // to Dolphin, so the slot simply takes none.
    for (const char *signal : {SIGNAL(itemsInserted(KItemRangeList)), SIGNAL(itemsRemoved(KItemRangeList)), SIGNAL(itemsMoved(KItemRange, QList<int>)),
                               SIGNAL(directoryLoadingCompleted()), SIGNAL(fileItemsChanged(KFileItemList))}) {
        connect(model, signal, this, SLOT(onModelChanged()));
    }
}

void Bridge::onModelChanged()
{
    if (!m_sessionId.isEmpty() && m_sessionView) {
        m_listTimer.start();
    }
}

void Bridge::sendUpdatedList()
{
    if (m_sessionId.isEmpty() || !m_sessionView) {
        return;
    }
    const auto files = DolphinAccess::instance().files(m_sessionView);
    if (!files) {
        return;
    }
    QStringList urls;
    urls.reserve(files->urls.size());
    m_localToModelUrl.clear();
    for (int i = 0; i < files->urls.size(); ++i) {
        const QString local = files->urls.at(i).toString();
        urls << local;
        m_localToModelUrl.insert(local, files->modelUrls.at(i));
    }
    const QString key = urls.join(u'\n');
    if (key == m_lastSentListKey) {
        return; // e.g. only metadata of an item changed
    }
    m_lastSentListKey = key;
    m_sessionUrls = urls;
    sendToViewer(u"UpdateList"_s, {m_sessionId, urls});
}

void Bridge::syncSelection(const QString &url)
{
    if (!m_syncSelection || !m_sessionView) {
        return;
    }
    const QUrl modelUrl = m_localToModelUrl.value(url, QUrl(url));
    m_applyingSync = true;
    DolphinAccess::instance().selectUrl(m_sessionView, modelUrl);
    m_applyingSync = false;
}

void Bridge::onViewerCurrentChanged(const QString &session, const QString &url)
{
    if (session != m_sessionId) {
        return; // another Dolphin window/process opened that preview
    }
    m_viewerCurrent = url;
    syncSelection(url);
}

void Bridge::onViewerClosed(const QString &session, const QString &url)
{
    if (session != m_sessionId) {
        return;
    }
    syncSelection(url);
    if (m_sessionView) {
        // Give the keyboard focus back to the file view inside Dolphin; the
        // compositor reactivates Dolphin's window.
        if (QWidget *container = DolphinAccess::containerOf(m_sessionView)) {
            container->setFocus(Qt::OtherFocusReason);
        }
    }
    endSession();
}

void Bridge::endSession()
{
    m_sessionId.clear();
    m_viewerCurrent.clear();
    m_listTimer.stop();
    if (m_sessionModel) {
        disconnect(m_sessionModel, nullptr, this, nullptr);
    }
    m_sessionModel.clear();
    if (m_sessionView) {
        disconnect(m_sessionView, SIGNAL(selectionChanged(KFileItemList)), this, SLOT(onSessionSelectionChanged(KFileItemList)));
    }
}

void Bridge::onSessionSelectionChanged(const KFileItemList &selection)
{
    if (m_applyingSync || m_sessionId.isEmpty() || sender() != m_sessionView || selection.size() != 1 || selection.first().isDir()) {
        return;
    }
    const QString url = selection.first().mostLocalUrl().toString();
    const qsizetype index = m_sessionUrls.indexOf(url);
    if (url == m_viewerCurrent || index < 0) {
        return;
    }
    m_viewerCurrent = url;
    sendToViewer(u"Open"_s, {m_sessionUrls, int(index), QVariantMap{{u"session"_s, m_sessionId}, {u"origin"_s, u"follow"_s}}});
}

void Bridge::trackFallbackSelection(QWidget *view)
{
    if (m_trackedViews.contains(view)) {
        return;
    }
    if (connect(view, SIGNAL(selectionChanged(KFileItemList)), this, SLOT(onFallbackSelectionChanged(KFileItemList)))) {
        m_trackedViews.insert(view);
        connect(view, &QObject::destroyed, this, [this, view] {
            m_trackedViews.remove(view);
            m_fallbackSelection.remove(view);
        });
    }
}

void Bridge::onFallbackSelectionChanged(const KFileItemList &selection)
{
    if (auto *view = qobject_cast<QWidget *>(sender())) {
        m_fallbackSelection.insert(view, selection);
    }
}

QWidget *Bridge::activeFileView() const
{
    QWidget *view = nullptr;
    if (DolphinAccess::isFileViewFocus(QApplication::focusWidget(), &view)) {
        return view;
    }
    // Dolphin is not the active window (e.g. a test runs in a terminal): use
    // the file view the active Dolphin window would give the focus back to.
    const auto windows = QApplication::topLevelWidgets();
    for (QWidget *w : windows) {
        if (w->isVisible() && w->inherits("DolphinMainWindow") && DolphinAccess::isFileViewFocus(w->focusWidget(), &view)) {
            return view;
        }
    }
    return nullptr;
}

QString Bridge::State()
{
    const DolphinAccess &access = DolphinAccess::instance();
    QJsonObject o{
        {u"modelAccess"_s, access.hasModelAccess()},
        {u"selectionAccess"_s, access.hasSelectionAccess()},
        {u"missingSymbols"_s, QJsonArray::fromStringList(access.missingSymbols())},
        {u"session"_s, m_sessionId},
        {u"sessionViewAlive"_s, !m_sessionView.isNull()},
        {u"hotkey"_s, QKeySequence(m_hotkey).toString(QKeySequence::PortableText)},
        {u"syncSelection"_s, m_syncSelection},
        {u"dolphinVersion"_s, QCoreApplication::applicationVersion()},
    };
    QWidget *focusView = nullptr;
    o.insert(u"focusInFileView"_s, DolphinAccess::isFileViewFocus(QApplication::focusWidget(), &focusView));
    if (QWidget *view = m_sessionView ? m_sessionView.data() : activeFileView()) {
        if (auto files = access.files(view)) {
            o.insert(u"viewFileCount"_s, int(files->urls.size()));
        }
        if (auto snap = access.snapshot(view)) {
            o.insert(u"target"_s, snap->urls.value(snap->current).toString());
        }
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool Bridge::TriggerFocusedView()
{
    QWidget *view = activeFileView();
    return view && trigger(view, Origin::Hotkey);
}

} // namespace KDEGlance
