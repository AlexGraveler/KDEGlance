// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "navigationlist.h"
#include "settings.h"
#include "views/contentview.h"

#include <QDBusVariant>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QVariantMap>

namespace KDEGlance
{
class PreviewWindow;
class KWinIntegration;
class SettingsDialog;

/*
 * The resident viewer service (D-Bus name org.kde.kdeglance, object /Viewer).
 *
 * One process serves every request: the Dolphin hook, the context menu and
 * the command line all call Open/Toggle; the window and its decoders are
 * reused. While no preview is shown the process only waits on D-Bus (no
 * timers, no polling).
 *
 * A "session" is one preview from opening to closing. Requests from Dolphin
 * carry a session id; CurrentChanged/Closed signals carry it back so the
 * Dolphin instance that opened the preview can follow along (selection sync)
 * while other Dolphin windows ignore them.
 */
class ViewerApp : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeglance.Viewer")
public:
    explicit ViewerApp(QObject *parent = nullptr);
    ~ViewerApp() override;

    // Registers org.kde.kdeglance and /Viewer; false if another instance runs.
    bool registerOnBus();
    void startDesktopIntegration(); // KWin placement script
    void shutdown();

    PreviewWindow *window() const;
    Settings *settings();
    const NavigationList &navigation() const;
    QString session() const;

    // C++ entry used by D-Bus and the command line.
    void open(const QList<QUrl> &urls, int index, const QVariantMap &options, bool toggle);

    static PreviewItem makeItem(const QUrl &url);

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &urls, int index, const QVariantMap &options);
    Q_SCRIPTABLE void Toggle(const QStringList &urls, int index, const QVariantMap &options);
    Q_SCRIPTABLE void UpdateList(const QString &session, const QStringList &urls);
    Q_SCRIPTABLE void Close();
    Q_SCRIPTABLE bool IsVisible();
    Q_SCRIPTABLE QString CurrentUrl();
    Q_SCRIPTABLE QString Status();
    Q_SCRIPTABLE void ShowSettings();
    Q_SCRIPTABLE void ResetSettings();
    // Scripting/tests: change one setting (names as in settings.json).
    Q_SCRIPTABLE bool SetOption(const QString &name, const QDBusVariant &value);
    Q_SCRIPTABLE void ReportPhotoWindowState(const QString &session, bool managed);
    Q_SCRIPTABLE void ReportWorkAreas(const QString &json);
    Q_SCRIPTABLE void Navigate(int delta);
    Q_SCRIPTABLE void Ping();
    Q_SCRIPTABLE void Quit();

Q_SIGNALS:
    Q_SCRIPTABLE void CurrentChanged(const QString &session, const QString &url);
    Q_SCRIPTABLE void Closed(const QString &session, const QString &url);
    Q_SCRIPTABLE void SettingsChanged();
    Q_SCRIPTABLE void VisibilityChanged(bool visible);

private:
    void showCurrent(bool newSession);
    void onWindowClosed();
    void scheduleSave();
    void saveNow();
    void prefetchNeighbours();
    void applyWorkArea();
    void loadDirectoryAsync(const QUrl &current, const QString &session);

    Settings m_settings;
    PreviewWindow *m_window = nullptr;
    KWinIntegration *m_kwin = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    NavigationList m_nav;
    QString m_session;
    quint64 m_localSessions = 0;
    QString m_targetScreen;
    QHash<QString, QRect> m_workAreas;
    QString m_activeScreen;
    QTimer m_saveTimer;
    QByteArray m_lastSaved;
};

} // namespace KDEGlance
