// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

// Helpers to create real sample files for the tests.

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTest>

#include <functional>

namespace KDEGlanceTest
{

inline QString ffmpeg()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

inline QString imagemagick()
{
    QString m = QStandardPaths::findExecutable(QStringLiteral("magick"));
    if (m.isEmpty()) {
        m = QStandardPaths::findExecutable(QStringLiteral("convert"));
    }
    return m;
}

inline bool run(const QString &program, const QStringList &args, int timeoutMs = 60000)
{
    if (program.isEmpty()) {
        return false;
    }
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning().noquote() << program << args.join(QLatin1Char(' ')) << "failed:" << p.readAll().right(800);
        return false;
    }
    return true;
}

// Left half red, right half blue.
inline QImage twoColorImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    QPainter p(&img);
    p.fillRect(0, 0, w / 2, h, Qt::red);
    p.fillRect(w / 2, 0, w - w / 2, h, Qt::blue);
    return img;
}

/*
 * Writes a JPEG and inserts an EXIF APP1 segment carrying only the
 * Orientation tag (1..8), right after the SOI marker.
 */
inline bool writeJpegWithOrientation(const QString &path, const QImage &image, quint16 orientation)
{
    QByteArray jpeg;
    {
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        QImageWriter writer(&buffer, "jpeg");
        writer.setQuality(95);
        if (!writer.write(image)) {
            return false;
        }
    }
    QByteArray tiff;
    tiff.append("II*\0", 4); // little endian TIFF
    tiff.append(char(8)).append(char(0)).append(char(0)).append(char(0)); // IFD0 offset
    tiff.append(char(1)).append(char(0)); // one entry
    tiff.append(char(0x12)).append(char(0x01)); // tag 0x0112 Orientation
    tiff.append(char(3)).append(char(0)); // SHORT
    tiff.append(char(1)).append(char(0)).append(char(0)).append(char(0)); // count 1
    tiff.append(char(orientation & 0xff)).append(char(orientation >> 8)).append(char(0)).append(char(0));
    tiff.append(char(0)).append(char(0)).append(char(0)).append(char(0)); // no next IFD
    QByteArray app1;
    app1.append("Exif\0\0", 6);
    app1.append(tiff);
    const int length = app1.size() + 2;
    QByteArray segment;
    segment.append(char(0xFF)).append(char(0xE1)).append(char(length >> 8)).append(char(length & 0xff));
    segment.append(app1);
    jpeg.insert(2, segment);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    return f.write(jpeg) == jpeg.size();
}

inline bool waitFor(const std::function<bool()> &condition, int timeoutMs = 10000)
{
    QElapsedTimer t;
    t.start();
    while (!condition()) {
        if (t.elapsed() > timeoutMs) {
            return false;
        }
        QTest::qWait(10);
    }
    return true;
}

} // namespace KDEGlanceTest
