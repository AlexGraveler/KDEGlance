// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "settings.h"

#include <KFileItem>

#include <QElapsedTimer>
#include <QHash>
#include <QKeyCombination>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>

class QKeyEvent;
class QWidget;

namespace KDEGlance
{

/*
 * Lives inside the Dolphin process (one per process, owned by qApp) and
 * connects Dolphin with the KDEGlance viewer service.
 *
 *  - Hotkey: an application-wide event filter sees the configured key
 *    (Space by default) before Dolphin's own shortcuts. When the keyboard
 *    focus is in a file view and Dolphin is not in type-ahead search or
 *    selection mode, the key opens/closes the preview; everywhere else it
 *    is left alone. Nothing is registered with KXmlGui, so the user's
 *    Dolphin shortcuts are never touched and nothing depends on menus being
 *    built.
 *  - Context menu: the "KDEGlance: Quick Look" action calls
 *    openFromContextMenu() (found through a qApp property), i.e. the same
 *    code path as the hotkey.
 *  - Navigation order: the files of the view in display order are sent with
 *    the request and re-sent (debounced) when the view's model changes.
 *  - Selection sync: CurrentChanged/Closed signals of the viewer select the
 *    previewed file in the view that opened the preview, and only there.
 *    The bridge never reacts to the selection changes it causes itself, so
 *    there are no update loops.
 *
 * All D-Bus calls from here are asynchronous: Dolphin never waits for the
 * viewer, even if it hangs or is being started.
 */
class Bridge : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeglance.Bridge")
public:
    // Creates the bridge once, if running inside Dolphin.
    static Bridge *ensure();
    ~Bridge() override;

    // Used by the context menu plugin (through QMetaObject::invokeMethod).
    Q_INVOKABLE bool openFromContextMenu(const QList<QUrl> &urls, QWidget *window);

public Q_SLOTS:
    // Diagnostics and desktop integration tests (D-Bus, /KDEGlanceBridge).
    Q_SCRIPTABLE QString State();
    Q_SCRIPTABLE bool TriggerFocusedView();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void onViewerCurrentChanged(const QString &session, const QString &url);
    void onViewerClosed(const QString &session, const QString &url);
    void onViewerSettingsChanged();
    void onModelChanged();
    void onFallbackSelectionChanged(const KFileItemList &selection);
    void onSessionSelectionChanged(const KFileItemList &selection);

private:
    explicit Bridge(QObject *parent);

    enum class Origin {
        Hotkey,
        ContextMenu,
    };
    bool matchesHotkey(const QKeyEvent *event) const;
    bool canHandleKeyIn(QWidget *view) const;
    bool trigger(QWidget *view, Origin origin, const std::optional<QUrl> &preferred = std::nullopt);
    void sendToViewer(const QString &method, const QVariantList &args);
    void watchModel(QWidget *view);
    void endSession();
    void sendUpdatedList();
    void syncSelection(const QString &url);
    void trackFallbackSelection(QWidget *view);
    void loadSettings();
    QWidget *activeFileView() const;
    void shutdown();

    Settings m_settings;
    QKeyCombination m_hotkey;
    bool m_syncSelection = true;

    QPointer<QWidget> m_sessionView;
    QPointer<QObject> m_sessionModel;
    QString m_sessionId;
    quint64 m_sessionCounter = 0;
    QHash<QString, QUrl> m_localToModelUrl;
    QStringList m_sessionUrls; // list last sent to the viewer
    QString m_viewerCurrent; // file the viewer shows
    bool m_applyingSync = false; // selection changes caused by us
    QString m_lastSentListKey;
    QTimer m_listTimer;

    QElapsedTimer m_lastTypeAheadKey;
    QHash<QWidget *, KFileItemList> m_fallbackSelection;
    QSet<QWidget *> m_trackedViews;
    bool m_down = false;
};

} // namespace KDEGlance
