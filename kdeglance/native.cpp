// SPDX-License-Identifier: GPL-3.0-or-later
// Optional, narrow bridge to public KDE APIs; never manipulates KWin internals.
#include <QWindow>
#include <QString>
#include <KWindowSystem>
extern "C" void kdeglance_activate(void *window, const char *token) {
    if (!window) return;
    if (token && *token)
        KWindowSystem::setCurrentXdgActivationToken(QString::fromUtf8(token));
    KWindowSystem::activateWindow(static_cast<QWindow *>(window));
}
