// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace KDEGlance::MidiSupport {
inline QString synthesizer() { return QStandardPaths::findExecutable(QStringLiteral("fluidsynth")); }
inline QString soundfont()
{
    const QString override = qEnvironmentVariable("KDEGLANCE_SOUNDFONT");
    if (!override.isEmpty())
        return QFileInfo(override).isReadable() ? override : QString();
    for (const auto &dir : {QStringLiteral("/usr/share/soundfonts"), QStringLiteral("/usr/share/sounds/sf2")}) {
        const auto fonts = QDir(dir).entryList({QStringLiteral("*.sf2"), QStringLiteral("*.sf3")}, QDir::Files | QDir::Readable);
        if (!fonts.isEmpty())
            return QDir(dir).filePath(fonts.first());
    }
    return {};
}
inline QStringList arguments(const QString &wav, const QString &font, const QString &midi)
{
    return {QStringLiteral("-n"), QStringLiteral("-i"), QStringLiteral("-q"), QStringLiteral("-T"), QStringLiteral("wav"),
            QStringLiteral("-r"), QStringLiteral("44100"), QStringLiteral("-F"), wav, font, midi};
}
}
