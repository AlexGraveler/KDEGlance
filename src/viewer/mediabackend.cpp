// SPDX-License-Identifier: GPL-3.0-or-later
#include "mediabackend.h"
#include "formatprobe.h"
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <cstdio>

namespace KDEGlance {
int checkVideoDecoder()
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    const QString path = dir.filePath(QStringLiteral("av1.mkv"));
    if (!QFile::copy(QStringLiteral(":/decoder-check/av1.mkv"), path))
        return 1;
    return FormatProbe::probeFile(path, 2000).ok ? 0 : 1;
}

void configureMediaBackend()
{
    // An empty value means "use defaults" in Qt; a comma means no devices.
    if (qgetenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES") == ",")
        return;
    // Test the real Qt backend in a disposable process. A failed hardware AV1
    // decoder can produce neither a frame nor an error signal; changing the
    // environment after that failure cannot reset Qt's cached device list.
    QProcess check;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    check.setProcessEnvironment(env);
    check.setStandardOutputFile(QProcess::nullDevice());
    check.setStandardErrorFile(QProcess::nullDevice());
    check.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--check-video-decoder")});
    const bool finished = check.waitForFinished(4000);
    if (!finished || check.exitStatus() != QProcess::NormalExit || check.exitCode() != 0) {
        check.kill();
        check.waitForFinished(1000);
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
        std::fprintf(stderr, "KDEGlance: Qt hardware decoder check failed; using software decoding.\n");
    }
}
}
