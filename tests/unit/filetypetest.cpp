// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "filetype.h"
#include "navigationlist.h"

#include "../testutils.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace KDEGlance;
using namespace Qt::StringLiterals;

class FileTypeTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString file(const QString &name) const
    {
        return m_dir.filePath(name);
    }
    void write(const QString &name, const QByteArray &data)
    {
        QFile f(file(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

private Q_SLOTS:
    void contentWinsOverExtension()
    {
        QImage img(10, 10, QImage::Format_RGB32);
        img.fill(Qt::green);
        QVERIFY(img.save(file(u"really-a-png.jpg"_s), "PNG"));
        const FileTypeInfo t = detectFileType(file(u"really-a-png.jpg"_s));
        QCOMPARE(t.kind, ContentKind::Image);
        QCOMPARE(t.mimeType.name(), u"image/png"_s);
        QVERIFY(t.extensionMismatch);
    }

    void rawByExtensionOnTiffContent()
    {
        // Most RAW formats are TIFF containers; the extension refines that.
        QImage img(16, 16, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QVERIFY(img.save(file(u"camera.tif"_s), "TIFF"));
        QFile::copy(file(u"camera.tif"_s), file(u"IMG_0001.NEF"_s));
        QFile::copy(file(u"camera.tif"_s), file(u"shot.srw"_s)); // not in shared-mime-info
        QCOMPARE(detectFileType(file(u"IMG_0001.NEF"_s)).kind, ContentKind::Raw);
        QCOMPARE(detectFileType(file(u"shot.srw"_s)).kind, ContentKind::Raw);
        QCOMPARE(detectFileType(file(u"camera.tif"_s)).kind, ContentKind::Image);
    }

    void svg()
    {
        write(u"vector.svg"_s, QByteArray("<svg xmlns=\"http:/" "/www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>"));
        QCOMPARE(detectFileType(file(u"vector.svg"_s)).kind, ContentKind::Svg);
    }

    void realMediaMetafile()
    {
        write(u"stream.ram"_s, "# comment\nrtsp://example.org/live.rm\nhttp://example.org/fallback.ra\n");
        const FileTypeInfo t = detectFileType(file(u"stream.ram"_s));
        QCOMPARE(t.kind, ContentKind::StreamLink);
        QCOMPARE(t.streamUrls.size(), 2);
        QCOMPARE(t.streamUrls.first(), u"rtsp://example.org/live.rm"_s);

        write(u"binary.ram"_s, QByteArray(".RMF\0\0\0\x12\0\x01", 10) + QByteArray(64, 'x'));
        QCOMPARE(detectFileType(file(u"binary.ram"_s)).kind, ContentKind::Video);
    }

    void streamParser()
    {
        QCOMPARE(parseStreamLinkFile("pnm://a/b.ra\n"), QStringList{u"pnm://a/b.ra"_s});
        QVERIFY(parseStreamLinkFile(QByteArray("\0binary", 7)).isEmpty());
        QVERIFY(parseStreamLinkFile("just some text").isEmpty());
    }

    void midiAndText()
    {
        write(u"song.mid"_s, QByteArray("MThd\0\0\0\x06\0\x01\0\x01\0\x60", 14));
        QCOMPARE(detectFileType(file(u"song.mid"_s)).kind, ContentKind::Midi);
        write(u"notes.txt"_s, "hello");
        QCOMPARE(detectFileType(file(u"notes.txt"_s)).kind, ContentKind::Text);
    }

    void directoryAndMissing()
    {
        QDir(m_dir.path()).mkdir(u"folder"_s);
        QCOMPARE(detectFileType(file(u"folder"_s)).kind, ContentKind::Directory);
        QCOMPARE(detectFileType(file(u"does-not-exist.png"_s)).kind, ContentKind::Missing);
    }

    void mediaWithFfmpeg()
    {
        const QString ff = KDEGlanceTest::ffmpeg();
        if (ff.isEmpty()) {
            QSKIP("ffmpeg not installed");
        }
        QVERIFY(KDEGlanceTest::run(ff, {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=0.5"_s, file(u"a.wav"_s)}));
        QCOMPARE(detectFileType(file(u"a.wav"_s)).kind, ContentKind::Audio);
        // A WAV renamed to .mp4 is still audio by content.
        QFile::copy(file(u"a.wav"_s), file(u"misnamed.mp4"_s));
        QCOMPARE(detectFileType(file(u"misnamed.mp4"_s)).kind, ContentKind::Audio);
        QVERIFY(KDEGlanceTest::run(ff, {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=duration=0.5:size=64x48"_s, u"-c:v"_s,
                                        u"mpeg4"_s, file(u"v.mkv"_s)}));
        QCOMPARE(detectFileType(file(u"v.mkv"_s)).kind, ContentKind::Video);
    }

    void sizeCategories()
    {
        QCOMPARE(sizeCategoryFor(ContentKind::Image), SizeCategory::Image);
        QCOMPARE(sizeCategoryFor(ContentKind::Raw), SizeCategory::Image);
        QCOMPARE(sizeCategoryFor(ContentKind::Video), SizeCategory::Video);
        QCOMPARE(sizeCategoryFor(ContentKind::Audio), SizeCategory::Audio);
    }

    // ---- navigation ------------------------------------------------------------
    void navigationSteps()
    {
        const QList<QUrl> urls{QUrl(u"file:///a"_s), QUrl(u"file:///b"_s), QUrl(u"file:///c"_s)};
        NavigationList n;
        n.reset(urls, urls.at(1));
        QCOMPARE(n.index(), 1);
        QVERIFY(n.step(1));
        QCOMPARE(n.current(), urls.at(2));
        QVERIFY(!n.step(1)); // stops at the end
        QCOMPARE(n.current(), urls.at(2));
        QVERIFY(n.step(-1));
        QVERIFY(n.step(-1));
        QVERIFY(!n.step(-1));
        QCOMPARE(n.current(), urls.at(0));
    }

    void navigationCurrentAlwaysReachable()
    {
        NavigationList n;
        n.reset({QUrl(u"file:///a"_s)}, QUrl(u"file:///z"_s));
        QCOMPARE(n.count(), 2);
        QCOMPARE(n.current(), QUrl(u"file:///z"_s));
    }

    void navigationGapAfterDeletion()
    {
        const QUrl a(u"file:///a"_s), b(u"file:///b"_s), c(u"file:///c"_s);
        NavigationList n;
        n.reset({a, b, c}, b);
        n.update({a, c}); // b deleted while shown
        QVERIFY(n.currentIsMissing());
        QCOMPARE(n.peek(1), std::optional<QUrl>(c));
        QCOMPARE(n.peek(-1), std::optional<QUrl>(a));
        QVERIFY(n.step(1));
        QCOMPARE(n.current(), c);
        QVERIFY(!n.currentIsMissing());
    }

    void navigationReorder()
    {
        const QUrl a(u"file:///a"_s), b(u"file:///b"_s), c(u"file:///c"_s);
        NavigationList n;
        n.reset({a, b, c}, a);
        n.update({c, b, a}); // sort order changed in Dolphin
        QCOMPARE(n.index(), 2);
        QVERIFY(!n.step(1));
        QVERIFY(n.step(-1));
        QCOMPARE(n.current(), b);
    }

    void directoryFallbackOrder()
    {
        QTemporaryDir dir;
        for (const QString &name : {u"img10.png"_s, u"img2.png"_s, u"Img1.png"_s, u".hidden.png"_s}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QDir(dir.path()).mkdir(u"sub"_s);
        const QList<QUrl> list = listDirectoryForNavigation(QUrl::fromLocalFile(dir.filePath(u"img2.png"_s)));
        QStringList names;
        for (const QUrl &u : list) {
            names << u.fileName();
        }
        QCOMPARE(names, (QStringList{u"Img1.png"_s, u"img2.png"_s, u"img10.png"_s}));
        const QList<QUrl> withHidden = listDirectoryForNavigation(QUrl::fromLocalFile(dir.filePath(u".hidden.png"_s)));
        QCOMPARE(withHidden.size(), 4);
    }
};

QTEST_GUILESS_MAIN(FileTypeTest)
#include "filetypetest.moc"
