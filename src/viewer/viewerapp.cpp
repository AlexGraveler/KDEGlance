// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "viewerapp.h"

#include "constants.h"
#include "defaultfileopener.h"
#include "l10n.h"
#include "kwinintegration.h"
#include "previewwindow.h"
#include "settingsdialog.h"
#include "views/imageview.h"
#include "views/infoview.h"
#include "views/mediaview.h"
#include "views/textview.h"
#include "views/pdfview.h"
#include "views/officeview.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QScreen>
#include <QThreadPool>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcViewer, "kdeglance.viewer", QtInfoMsg)

namespace KDEGlance
{

ViewerApp::ViewerApp(QObject *parent)
    : QObject(parent)
{
    // KIO jobs hold QEventLoopLocker instances. Releasing the last locker after
    // Enter hides the preview can otherwise quit QApplication despite
    // quitOnLastWindowClosed(false). This is a resident D-Bus service.
    QCoreApplication::setQuitLockEnabled(false);
    const Settings::LoadStatus status = m_settings.load();
    if (status == Settings::LoadStatus::RecoveredFromBackup || status == Settings::LoadStatus::ResetCorrupt) {
        qCWarning(lcViewer) << "settings file was damaged; recovered:" << (status == Settings::LoadStatus::RecoveredFromBackup);
        saveNow();
    } else if (status == Settings::LoadStatus::Migrated) {
        saveNow();
    }
    m_lastSaved = QJsonDocument(m_settings.toJson()).toJson(QJsonDocument::Compact);

    m_window = new PreviewWindow(&m_settings);
    connect(m_window, &PreviewWindow::closed, this, &ViewerApp::onWindowClosed);
    connect(m_window, &PreviewWindow::navigateRequested, this, &ViewerApp::Navigate);
    connect(m_window, &PreviewWindow::settingsModified, this, &ViewerApp::scheduleSave);
    connect(m_window, &PreviewWindow::settingsResetRequested, this, [this] {
        saveNow();
    });

    auto* opener = new DefaultFileOpener(this);
    connect(m_window, &PreviewWindow::openExternallyRequested, opener, &DefaultFileOpener::open);
    connect(opener, &DefaultFileOpener::failed, this, [](const QUrl&, const QString& error) {
        qCWarning(lcViewer) << "default application launch failed:" << error;
        auto* message = new QMessageBox(QMessageBox::Warning, tr_("KDEGlance"), error, QMessageBox::Ok);
        message->setAttribute(Qt::WA_DeleteOnClose);
        message->open(); // no exec(): the service keeps processing D-Bus requests
    });

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &ViewerApp::saveNow);

    // Screens appearing/disappearing or changing scale invalidate work areas.
    auto screensChanged = [this] {
        m_workAreas.clear();
        applyWorkArea();
    };
    connect(qApp, &QGuiApplication::screenAdded, this, screensChanged);
    connect(qApp, &QGuiApplication::screenRemoved, this, screensChanged);
}

ViewerApp::~ViewerApp()
{
    shutdown();
    delete m_window;
}

bool ViewerApp::registerOnBus()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcViewer) << "no D-Bus session bus";
        return false;
    }
    if (!bus.registerObject(QString(DBusViewerPath), this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        qCWarning(lcViewer) << "cannot register" << DBusViewerPath;
        return false;
    }
    if (!bus.registerService(QString(DBusService))) {
        bus.unregisterObject(QString(DBusViewerPath));
        return false;
    }
    return true;
}

void ViewerApp::startDesktopIntegration()
{
    if (!m_kwin) {
        m_kwin = new KWinIntegration(this);
        m_kwin->start();
    }
}

void ViewerApp::shutdown()
{
    if (m_saveTimer.isActive()) {
        saveNow();
    }
    if (m_window && m_window->isPreviewVisible()) {
        m_window->closePreview();
    }
    if (m_kwin) {
        m_kwin->stop();
    }
}

PreviewWindow *ViewerApp::window() const
{
    return m_window;
}

Settings *ViewerApp::settings()
{
    return &m_settings;
}

const NavigationList &ViewerApp::navigation() const
{
    return m_nav;
}

QString ViewerApp::session() const
{
    return m_session;
}

PreviewItem ViewerApp::makeItem(const QUrl &url)
{
    PreviewItem item;
    item.url = url;
    item.path = url.isLocalFile() ? url.toLocalFile() : QString();
    if (!item.path.isEmpty()) {
        item.type = detectFileType(item.path);
    } else {
        item.type = detectUrlType(url);
    }
    return item;
}

void ViewerApp::Open(const QStringList &urls, int index, const QVariantMap &options)
{
    QList<QUrl> list;
    list.reserve(urls.size());
    for (const QString &u : urls) {
        const QUrl url = QUrl::fromUserInput(u, QString(), QUrl::AssumeLocalFile);
        if (url.isValid()) {
            list << url;
        }
    }
    open(list, index, options, false);
}

void ViewerApp::Toggle(const QStringList &urls, int index, const QVariantMap &options)
{
    QList<QUrl> list;
    for (const QString &u : urls) {
        list << QUrl::fromUserInput(u, QString(), QUrl::AssumeLocalFile);
    }
    open(list, index, options, true);
}

void ViewerApp::open(const QList<QUrl> &urls, int index, const QVariantMap &options, bool toggle)
{
    if (urls.isEmpty()) {
        return;
    }
    index = std::clamp(index, 0, int(urls.size()) - 1);
    const QUrl current = urls.at(index);

    if (toggle && m_window->isPreviewVisible() && m_nav.current() == current) {
        m_window->closePreview();
        return;
    }

    const bool wasVisible = m_window->isPreviewVisible();
    const QString requestedSession = options.value(u"session"_s).toString();
    if (options.value(u"origin"_s).toString() == u"follow"_s && (!wasVisible || requestedSession != m_session)) {
        return; // a selection change in Dolphin after this preview was closed
    }
    if (wasVisible && !requestedSession.isEmpty() && requestedSession == m_session) {
        // Same session (e.g. the preview follows Dolphin's selection): just
        // show another file in the open window.
        m_nav.reset(urls, current);
        showCurrent(false);
        return;
    }
    if (wasVisible && !m_session.isEmpty() && m_session != requestedSession) {
        // A request from someone else replaces the current session.
        Q_EMIT Closed(m_session, m_nav.current().toString());
    }

    m_session = requestedSession;
    const bool fromBridge = !m_session.isEmpty();
    if (!fromBridge) {
        m_session = u"local-%1"_s.arg(++m_localSessions);
    }
    m_targetScreen = options.value(u"screen"_s).toString();
    applyWorkArea();

    m_nav.reset(urls, current);
    showCurrent(true);

    // A single file without a provider: step through its folder.
    if (!fromBridge && urls.size() == 1 && current.isLocalFile()) {
        loadDirectoryAsync(current, m_session);
    }
}

void ViewerApp::loadDirectoryAsync(const QUrl &current, const QString &session)
{
    QPointer<ViewerApp> self(this);
    QThreadPool::globalInstance()->start([self, current, session] {
        const QList<QUrl> list = listDirectoryForNavigation(current);
        QMetaObject::invokeMethod(
            qApp,
            [self, list, session] {
                if (self && self->m_session == session) {
                    self->m_nav.update(list);
                    self->prefetchNeighbours();
                }
            },
            Qt::QueuedConnection);
    });
}

void ViewerApp::showCurrent(bool newSession)
{
    const PreviewItem item = makeItem(m_nav.current());
    const bool wasVisible = m_window->isPreviewVisible();
    m_window->showItem(item, newSession);
    if (!wasVisible) {
        Q_EMIT VisibilityChanged(true);
    }
    prefetchNeighbours();
}

void ViewerApp::prefetchNeighbours()
{
    QList<PreviewItem> items;
    for (int delta : {1, -1, 2}) {
        if (auto url = m_nav.peek(delta); url && url->isLocalFile()) {
            items << makeItem(*url);
        }
    }
    m_window->prefetch(items);
}

void ViewerApp::Navigate(int delta)
{
    if (!m_window->isPreviewVisible() || delta == 0) {
        return;
    }
    if (!m_nav.step(delta > 0 ? 1 : -1)) {
        return; // at the first/last file
    }
    showCurrent(false);
    Q_EMIT CurrentChanged(m_session, m_nav.current().toString());
}

void ViewerApp::UpdateList(const QString &session, const QStringList &urls)
{
    if (session != m_session || !m_window->isPreviewVisible()) {
        return;
    }
    QList<QUrl> list;
    list.reserve(urls.size());
    for (const QString &u : urls) {
        list << QUrl(u);
    }
    m_nav.update(list);
    prefetchNeighbours();
}

void ViewerApp::Close()
{
    m_window->closePreview();
}

void ViewerApp::onWindowClosed()
{
    const QString session = m_session;
    const QString url = m_nav.current().toString();
    if (m_saveTimer.isActive() || QJsonDocument(m_settings.toJson()).toJson(QJsonDocument::Compact) != m_lastSaved) {
        saveNow();
    }
    Q_EMIT Closed(session, url);
    Q_EMIT VisibilityChanged(false);
}

bool ViewerApp::IsVisible()
{
    return m_window->isPreviewVisible();
}

QString ViewerApp::CurrentUrl()
{
    return m_window->isPreviewVisible() ? m_nav.current().toString() : QString();
}

QString ViewerApp::Status()
{
    QJsonObject o;
    o[u"visible"_s] = m_window->isPreviewVisible();
    o[u"windowShown"_s] = m_window->isVisible();
    o[u"session"_s] = m_session;
    o[u"url"_s] = m_nav.current().toString();
    o[u"localPath"_s] = m_window->currentItem().path;
    o[u"index"_s] = m_nav.index();
    o[u"count"_s] = m_nav.count();
    o[u"width"_s] = m_window->width();
    o[u"height"_s] = m_window->height();
    o[u"category"_s] = sizeCategoryKey(m_window->currentCategory());
    o[u"active"_s] = m_window->isActiveWindow();
    o[u"workArea"_s] = QJsonArray{m_window->workArea().x(), m_window->workArea().y(), m_window->workArea().width(), m_window->workArea().height()};
    QString view = u"none"_s;
    if (m_window->currentView() == m_window->imageView()) {
        view = u"image"_s;
        auto *iv = m_window->imageView();
        o[u"zoom"_s] = iv->zoomScale();
        o[u"imageRotation"_s]=iv->rotation();
        o[u"imageMirrored"_s]=iv->isMirrored();
        const auto r=iv->displayRect();
        o[u"imageRect"_s]=QJsonArray{r.x(),r.y(),r.width(),r.height()};
        o[u"fit"_s] = iv->isFitMode();
        o[u"animated"_s] = iv->isAnimated();
        o[u"hasImage"_s] = iv->hasImage();
        o[u"page"_s] = iv->currentPage();
        o[u"pages"_s] = iv->pageCount();
    } else if (m_window->currentView() == m_window->mediaView()) {
        view = u"media"_s;
        auto *mv = m_window->mediaView();
        o[u"volume"_s]=m_settings.moduleVolume(mv->sizeCategory());
        o[u"muted"_s]=m_settings.moduleMuted(mv->sizeCategory());
        o[u"loop"_s]=m_settings.moduleLoopVideo();
        o[u"hasVideo"_s] = mv->hasVideo();
        o[u"hasFrame"_s] = mv->surface()->hasFrame();
        o[u"videoFrames"_s] = mv->surface()->framesShown();
        o[u"videoRotation"_s] = mv->surface()->rotation();
        o[u"videoZoom"_s] = mv->surface()->zoom();
        o[u"playing"_s] = mv->player()->playbackState() == QMediaPlayer::PlayingState;
        o[u"loaded"_s] = mv->isLoaded();
        o[u"title"_s] = mv->title();
        o[u"artist"_s] = mv->artist();
        o[u"cover"_s] = mv->hasCoverArt();
        o[u"position"_s] = qint64(mv->player()->position());
        o[u"duration"_s] = qint64(mv->player()->duration());
    } else if (m_window->currentView() == m_window->infoView()) {
        view = u"info"_s;
        o[u"message"_s] = m_window->infoView()->message();
    }
    o[u"view"_s] = view;
    QJsonObject sizes;
    for (SizeCategory c : {SizeCategory::Image, SizeCategory::Video, SizeCategory::Audio, SizeCategory::Document, SizeCategory::Pdf, SizeCategory::Text, SizeCategory::Office}) {
        if (auto s = m_settings.savedSize(c)) {
            sizes[sizeCategoryKey(c)] = QJsonArray{s->width(), s->height()};
        }
    }
    if (m_window->currentView() == m_window->textView()) o[u"view"_s] = u"text"_s;
    if (m_window->currentView() == m_window->pdfView()) {
        o[u"view"_s] = u"pdf"_s;
        o[u"pages"_s] = m_window->pdfView()->pageCount();
        o[u"cachedPages"_s] = m_window->pdfView()->cachedPages();
        o[u"documentZoom"_s] = m_window->pdfView()->zoom();
        o[u"reflow"_s] = m_window->pdfView()->isReflow();
    }
    if (m_window->currentView() == m_window->officeView()) {
        o[u"view"_s] = u"office"_s;
        o[u"pages"_s] = m_window->officeView()->pdfView()->pageCount();
        o[u"converting"_s] = m_window->officeView()->converting();
        o[u"cachedPages"_s] = m_window->officeView()->pdfView()->cachedPages();
        o[u"documentZoom"_s] = m_window->officeView()->pdfView()->zoom();
        o[u"reflow"_s] = m_window->officeView()->pdfView()->isReflow();
    }
    o[u"savedSizes"_s] = sizes;
    o[u"syncSelection"_s] = m_settings.syncSelection();
    o[u"kwinScript"_s] = m_kwin && m_kwin->isScriptLoaded();
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void ViewerApp::ShowSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(&m_settings);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_settingsDialog, &SettingsDialog::changed, this, [this] {
            m_window->applySettings();
            saveNow();
        });
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void ViewerApp::ResetSettings()
{
    m_settings.resetToDefaults();
    m_window->applySettings();
    saveNow();
}

bool ViewerApp::SetOption(const QString &name, const QDBusVariant &value)
{
    const QVariant v = value.variant();
    if (name == u"syncSelection"_s) {
        m_settings.setSyncSelection(v.toBool());
    } else if (name == u"shortcut"_s) {
        const QKeySequence seq = QKeySequence::fromString(v.toString(), QKeySequence::PortableText);
        if (seq.isEmpty()) {
            return false;
        }
        m_settings.setShortcut(seq);
    } else if (name == u"closeOnFocusLoss"_s) {
        m_settings.setCloseOnFocusLoss(v.toBool());
    } else if (name == u"prestartService"_s) {
        m_settings.setPrestartService(v.toBool());
    } else if (name == u"autoplay"_s) {
        m_settings.setAutoplay(v.toBool());
    } else if (name == u"loopVideo"_s) {
        m_settings.setModuleLoopVideo(v.toBool());
    } else if (name == u"hardwareDecoding"_s) {
        m_settings.setHardwareDecoding(v.toBool());
    } else if (name == u"rawPreferEmbeddedPreview"_s) {
        m_settings.setRawPreferEmbeddedPreview(v.toBool());
    } else if (name == u"volume"_s) {
        m_settings.setVolume(v.toDouble());
    } else {
        return false;
    }
    m_window->applySettings();
    saveNow();
    return true;
}

void ViewerApp::ReportPhotoWindowState(const QString &session, bool managed)
{
    // Ignore a late report from a surface belonging to a closed preview.
    if(session!=m_session || !m_window->isPreviewVisible() || m_window->currentView()!=m_window->imageView()) return;
    m_window->setPhotoCompositorManaged(managed);
}

void ViewerApp::ReportWorkAreas(const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    m_activeScreen = o.value(u"active"_s).toString();
    m_workAreas.clear();
    const QJsonArray screens = o.value(u"screens"_s).toArray();
    for (const QJsonValue &v : screens) {
        const QJsonObject s = v.toObject();
        const QRect r(s.value(u"x"_s).toInt(), s.value(u"y"_s).toInt(), s.value(u"width"_s).toInt(), s.value(u"height"_s).toInt());
        if (r.isValid()) {
            m_workAreas.insert(s.value(u"name"_s).toString(), r);
        }
    }
    applyWorkArea();
}

void ViewerApp::applyWorkArea()
{
    QString name = m_targetScreen;
    if (name.isEmpty() || !m_workAreas.contains(name)) {
        name = m_activeScreen;
    }
    QRect area = m_workAreas.value(name);
    if (!area.isValid()) {
        // Fall back to Qt's idea of the screen.
        const auto screens = QGuiApplication::screens();
        for (QScreen *s : screens) {
            if (s->name() == m_targetScreen) {
                area = s->availableGeometry();
            }
        }
        if (!area.isValid() && QGuiApplication::primaryScreen()) {
            area = QGuiApplication::primaryScreen()->availableGeometry();
        }
    }
    m_window->setWorkArea(area);
}

void ViewerApp::Ping()
{
}

void ViewerApp::Quit()
{
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void ViewerApp::scheduleSave()
{
    m_saveTimer.start();
}

void ViewerApp::saveNow()
{
    m_saveTimer.stop();
    QString error;
    if (!m_settings.save(&error)) {
        qCWarning(lcViewer) << "cannot save settings:" << error;
        return;
    }
    m_lastSaved = QJsonDocument(m_settings.toJson()).toJson(QJsonDocument::Compact);
    Q_EMIT SettingsChanged();
}

} // namespace KDEGlance
