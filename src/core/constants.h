// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QLatin1StringView>

// Names shared by every KDEGlance component (viewer service, Dolphin hook,
// context menu plugin, KWin placement script, tests). Keep in sync with the
// files in data/ and src/kwin/placement.js.
namespace KDEGlance
{
inline constexpr QLatin1StringView AppId{"org.kde.kdeglance"};
inline constexpr QLatin1StringView DBusService{"org.kde.kdeglance"};
inline constexpr QLatin1StringView DBusViewerPath{"/Viewer"};
inline constexpr QLatin1StringView DBusViewerInterface{"org.kde.kdeglance.Viewer"};
inline constexpr QLatin1StringView DBusBridgePath{"/KDEGlanceBridge"};
inline constexpr QLatin1StringView DBusBridgeInterface{"org.kde.kdeglance.Bridge"};
inline constexpr QLatin1StringView KWinScriptName{"kdeglance-placement"};
// Property set on qApp inside Dolphin by the hook so that the context menu
// plugin can reach the same in-process bridge without linking to it.
inline constexpr const char *BridgeAppProperty = "_kdeglance_bridge";
}
