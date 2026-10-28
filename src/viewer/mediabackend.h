// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace KDEGlance {
// Run before creating any QMediaPlayer: Qt caches its decoder selection.
void configureMediaBackend();
int checkVideoDecoder();
}
