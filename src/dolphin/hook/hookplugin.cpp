// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

// Entry point of the KDEGlance integration inside Dolphin.
//
// Dolphin loads every "kf6/overlayicon" plugin when it creates a file view
// (before the first window is shown), in every Dolphin process, independent
// of menus or actions. KDEGlance uses this documented plugin type only as a
// reliable way to be loaded early; it never adds overlay icons. The actual
// work is done by the Bridge, which exists once per Dolphin process.

#include "bridge.h"

#include <KOverlayIconPlugin>

#include <QStringList>
#include <QUrl>

namespace KDEGlance
{

class HookPlugin : public KOverlayIconPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kdeglance.DolphinHook" FILE "kdeglance_dolphinhook.json")
public:
    explicit HookPlugin(QObject *parent = nullptr)
        : KOverlayIconPlugin(parent)
    {
        Bridge::ensure();
    }

    QStringList getOverlays(const QUrl &item) override
    {
        Q_UNUSED(item)
        return {};
    }
};

} // namespace KDEGlance

#include "hookplugin.moc"
