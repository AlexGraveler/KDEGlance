// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QObject>
#include <QString>

class QDBusServiceWatcher;

namespace KDEGlance
{

/*
 * Loads the window placement script (src/kwin/placement.js) into KWin via
 * KWin's D-Bus scripting interface while the viewer service runs, and
 * unloads it on exit. The script is not installed into KWin's configuration
 * and nothing in the user's settings is modified. If KWin restarts, the
 * script is loaded again. Without KWin (other desktops) this does nothing
 * and the compositor's own placement is used.
 */
class KWinIntegration : public QObject
{
    Q_OBJECT
public:
    explicit KWinIntegration(QObject *parent = nullptr);
    ~KWinIntegration() override;

    static QString locateScript();

    void start();
    void stop(); // synchronous, short timeout (called on exit)
    bool isScriptLoaded() const;

Q_SIGNALS:
    void scriptStateChanged(bool loaded);

private:
    void load();
    void loadNow();
    void run(int id, int attempt = 0);

    QDBusServiceWatcher *m_watcher = nullptr;
    QString m_scriptPath;
    bool m_loaded = false;
    bool m_started = false;
};

} // namespace KDEGlance
