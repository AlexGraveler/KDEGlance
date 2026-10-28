// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QString>

namespace KDEGlance
{
/*
 * Minimal built-in translation: user-visible strings are written in English
 * and looked up in a compiled-in table for the system language. This keeps
 * the build free of gettext/Linguist tooling. Unknown strings and languages
 * fall back to English. The language can be forced with KDEGLANCE_LANG=en|ru.
 */
QString tr_(const char *source);

// Language code in use ("en", "ru").
QString uiLanguage();
}
