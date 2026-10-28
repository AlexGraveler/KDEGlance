// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

// Load tests: many open/close cycles and fast navigation over mixed media,
// checking that memory does not grow without bound, no stale content is
// shown and the idle service uses no CPU.

#include "previewwindow.h"
#include "viewerapp.h"
#include "views/imageloader.h"
#include "views/imageview.h"
#include "views/mediaview.h"

#include "../testutils.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/resource.h>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

namespace
{
qint64 rssKiB()
{
    QFile f(u"/proc/self/status"_s);
    if (!f.open(QIODevice::ReadOnly)) {
        return -1;
    }
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.startsWith("VmRSS:")) {
            return line.mid(6).trimmed().split(' ').first().toLongLong();
        }
    }
    return -1;
}

double cpuSeconds()
{
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
}
} // namespace

class StressTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QStringList m_files;

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"config"_s).toUtf8());
        for (int i = 0; i < 12; ++i) {
            const QString f = m_dir.filePath(u"img%1.jpg"_s.arg(i, 2, 10, QLatin1Char('0')));
            QImage img(1600 + i * 100, 1200, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(i * 30, 180, 220));
            QVERIFY(img.save(f, "JPEG"));
            m_files << QUrl::fromLocalFile(f).toString();
        }
        if (!ffmpeg().isEmpty()) {
            const QString v = m_dir.filePath(u"v.mkv"_s);
            const QString a = m_dir.filePath(u"a.flac"_s);
            if (run(ffmpeg(), {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=640x360:rate=25"_s, u"-t"_s, u"2"_s, u"-c:v"_s,
                               u"mpeg4"_s, v})) {
                m_files.insert(3, QUrl::fromLocalFile(v).toString());
            }
            if (run(ffmpeg(), {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=2"_s, a})) {
                m_files.insert(7, QUrl::fromLocalFile(a).toString());
            }
        }
        const QString im = imagemagick();
        if (!im.isEmpty()) {
            const QString g = m_dir.filePath(u"anim.gif"_s);
            if (run(im, {u"-delay"_s, u"5"_s, u"-size"_s, u"300x200"_s, u"xc:red"_s, u"xc:green"_s, u"xc:blue"_s, u"-loop"_s, u"0"_s, g})) {
                m_files.insert(5, QUrl::fromLocalFile(g).toString());
            }
        }
    }

    void openCloseCycles()
    {
        ViewerApp app;
        // Warm up (first decoders and plugins are loaded once).
        for (int i = 0; i < 10; ++i) {
            app.Open(m_files, i % m_files.size(), {{u"session"_s, u"warm"_s}});
            QTest::qWait(30);
            app.Close();
        }
        QTest::qWait(300);
        const qint64 before = rssKiB();
        for (int i = 0; i < 300; ++i) {
            app.Open(m_files, i % m_files.size(), {{u"session"_s, u"s%1"_s.arg(i)}});
            QVERIFY(app.IsVisible());
            QTest::qWait(i % 10 == 0 ? 60 : 5);
            app.Close();
            QVERIFY(!app.IsVisible());
        }
        app.window()->imageView()->loader()->waitForIdle();
        QTest::qWait(500);
        const qint64 after = rssKiB();
        qInfo() << "RSS before" << before / 1024 << "MiB, after 300 cycles" << after / 1024 << "MiB";
        QVERIFY2(after - before < 80 * 1024, "memory keeps growing over open/close cycles");
    }

    void fastNavigationOverMixedMedia()
    {
        ViewerApp app;
        app.Open(m_files, 0, {{u"session"_s, u"nav"_s}});
        for (int round = 0; round < 25; ++round) {
            for (int i = 0; i < m_files.size(); ++i) {
                app.Navigate(round % 2 ? -1 : 1);
                QTest::qWait(1);
            }
        }
        // 25 sweeps end at the last file.
        QCOMPARE(app.CurrentUrl(), m_files.last());
        QVERIFY(waitFor([&] {
            return app.window()->imageView()->hasImage();
        }));
        app.window()->imageView()->loader()->waitForIdle();
        QTest::qWait(100);
        QCOMPARE(app.window()->currentView(), app.window()->imageView());
        QCOMPARE(app.window()->imageView()->naturalSize(), QSize(1600 + 11 * 100, 1200));
        app.Close();
        // Media decoders are released when the preview closes.
        QVERIFY(app.window()->mediaView()->player()->source().isEmpty());
    }

    void idleServiceUsesNoCpu()
    {
        ViewerApp app;
        app.Open(m_files, 0, {{u"session"_s, u"idle"_s}});
        QTest::qWait(200);
        app.Close();
        QTest::qWait(300);
        const double start = cpuSeconds();
        QTest::qWait(2000);
        const double used = cpuSeconds() - start;
        qInfo() << "CPU time while idle for 2 s:" << used << "s";
        QVERIFY2(used < 0.05, "the idle service must not poll");
    }
};

QTEST_MAIN(StressTest)
#include "stresstest.moc"
