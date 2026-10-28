// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QList>
#include <QString>

namespace KDEGlance
{

/*
 * Verifies that sample files really decode with the decoders installed on
 * this machine (spec 9.4, rule 10): images are decoded, animations must yield
 * two frames, videos must deliver a frame and audio must deliver decoded
 * sample buffers. Used by `kdeglance --probe-formats DIR` and by the tests.
 */
struct ProbeResult {
    QString file;
    QString extension;
    QString kind;
    QString tier; // from FormatRegistry; empty if not catalogued
    bool ok = false;
    QString detail;
};

namespace FormatProbe
{
ProbeResult probeFile(const QString &path, int timeoutMs = 15000);
QList<ProbeResult> probeDirectory(const QString &dir, int timeoutMs = 15000);
// Prints a table (or JSON); returns the process exit code: non-zero if a
// mandatory format failed.
int runCli(const QString &dir, bool json);
}

} // namespace KDEGlance
