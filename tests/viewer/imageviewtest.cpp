// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "views/animationplayer.h"
#include "views/imagedecoder.h"
#include "views/imageloader.h"
#include "views/imageview.h"
#include "viewerapp.h"

#include "../testutils.h"

#include <QElapsedTimer>
#include <QImageWriter>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class ImageViewTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const
    {
        return m_dir.filePath(name);
    }
    PreviewItem item(const QString &name) const
    {
        return ViewerApp::makeItem(QUrl::fromLocalFile(path(name)));
    }
    bool loadAndWait(ImageView &view, const QString &name)
    {
        QSignalSpy ready(&view, &ContentView::ready);
        QSignalSpy failed(&view, &ContentView::failed);
        view.load(item(name));
        return waitFor([&] {
            return !ready.isEmpty() || !failed.isEmpty();
        }) && failed.isEmpty();
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(twoColorImage(400, 200).save(path(u"wide.png"_s)));
        QImage big(6000, 4000, QImage::Format_RGB32);
        big.fill(QColor(30, 120, 200));
        QVERIFY(QImageWriter(path(u"big.jpg"_s), "jpeg").write(big));
        for (int o = 1; o <= 8; ++o) {
            QVERIFY(writeJpegWithOrientation(path(u"orient%1.jpg"_s.arg(o)), twoColorImage(200, 100), quint16(o)));
        }
        QFile svg(path(u"vector.svg"_s));
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns=\"http:/" "/www.w3.org/2000/svg\" width=\"120\" height=\"60\"><rect width=\"60\" height=\"60\" fill=\"red\"/>"
                  "<rect x=\"60\" width=\"60\" height=\"60\" fill=\"blue\"/></svg>");
        svg.close();
        const QString im = imagemagick();
        if (!im.isEmpty()) {
            run(im, {u"-delay"_s, u"20"_s, u"-size"_s, u"60x40"_s, u"xc:red"_s, u"xc:green"_s, u"xc:blue"_s, u"-loop"_s, u"0"_s, path(u"anim.gif"_s)});
            run(im, {u"-delay"_s, u"0"_s, u"-size"_s, u"60x40"_s, u"xc:red"_s, u"xc:green"_s, u"-loop"_s, u"0"_s, path(u"fast.gif"_s)});
            run(im, {u"-size"_s, u"50x30"_s, u"xc:red"_s, u"xc:green"_s, u"xc:blue"_s, path(u"pages.tif"_s)});
        }
        QFile broken(path(u"broken.png"_s));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("\x89PNG\r\n\x1a\n garbage");
    }

    void uncompressedTgaAndTruncation()
    {
        // Valid type-2 TGA header is also a plausible ICO header. No encoder
        // is involved, so this guards content sniffing independently of FFmpeg.
        QByteArray tga(18, '\0');
        tga[2] = 2; tga[12] = 2; tga[14] = 1; tga[16] = 24; tga[17] = 32;
        tga += QByteArray::fromHex("0000ff00ff00"); // red, green in BGR order
        QFile file(path(u"tiny.tga"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(tga), tga.size());
        file.close();
        const auto decoded = ImageDecoder::decode(file.fileName(), ContentKind::Image, {}, 0, true, makeCancelToken());
        QVERIFY2(decoded.ok, qPrintable(decoded.error));
        QCOMPARE(decoded.image.size(), QSize(2, 1));
        QCOMPARE(decoded.image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(decoded.image.pixelColor(1, 0), QColor(Qt::green));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(tga.left(20));
        file.close();
        QVERIFY(!ImageDecoder::decode(file.fileName(), ContentKind::Image, {}, 0, true, makeCancelToken()).ok);
    }

    void clickZonesMatchOriginalSpecification()
    {
        ImageView view; view.resize(400,200);view.show();QVERIFY(loadAndWait(view,u"wide.png"_s));
        QSignalSpy nav(&view,&ContentView::navigateRequested),closed(&view,&ContentView::closeRequested);
        QTest::mouseClick(&view,Qt::LeftButton,{},QPoint(40,100));QVERIFY(waitFor([&]{return nav.size()==1;}));QCOMPARE(nav.last().first().toInt(),-1);
        QTest::mouseClick(&view,Qt::LeftButton,{},QPoint(360,100));QVERIFY(waitFor([&]{return nav.size()==2;}));QCOMPARE(nav.last().first().toInt(),1);
        QTest::mouseClick(&view,Qt::LeftButton,{},QPoint(200,100));QVERIFY(waitFor([&]{return closed.size()==1;}));
    }
    void dragReleaseIsNotAClick()
    {
        ImageView view;view.resize(400,200);view.show();QVERIFY(loadAndWait(view,u"wide.png"_s));view.zoomAt(3,QPointF(200,100));
        QSignalSpy closed(&view,&ContentView::closeRequested);QTest::mousePress(&view,Qt::LeftButton,{},QPoint(200,100));
        QTest::mouseRelease(&view,Qt::LeftButton,{},QPoint(280,100));QTest::qWait(QApplication::doubleClickInterval()+30);QVERIFY(closed.isEmpty());
    }
    void altDragMovesInsteadOfPanning()
    {
        ImageView view;view.resize(400,200);view.show();QVERIFY(loadAndWait(view,u"wide.png"_s));view.zoomAt(3,QPointF(200,100));const auto before=view.zoomOffset();
        QSignalSpy move(&view,&ContentView::systemMoveRequested);
        QTest::mousePress(&view,Qt::LeftButton,Qt::AltModifier,QPoint(200,100));QTest::mouseMove(&view,QPoint(260,120));QTest::mouseRelease(&view,Qt::LeftButton,Qt::AltModifier,QPoint(260,120));QCOMPARE(view.zoomOffset(),before); QCOMPARE(move.size(),1);
    }

    // EXIF Orientation: every value displays upright with the right size.
    void exifOrientation_data()
    {
        QTest::addColumn<int>("orientation");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QPoint>("redAt");
        QTest::newRow("1 normal") << 1 << QSize(200, 100) << QPoint(10, 50);
        QTest::newRow("2 mirrored") << 2 << QSize(200, 100) << QPoint(190, 50);
        QTest::newRow("3 rotate 180") << 3 << QSize(200, 100) << QPoint(190, 50);
        QTest::newRow("6 rotate 90 cw") << 6 << QSize(100, 200) << QPoint(50, 10);
        QTest::newRow("8 rotate 90 ccw") << 8 << QSize(100, 200) << QPoint(50, 190);
    }
    void exifOrientation()
    {
        QFETCH(int, orientation);
        QFETCH(QSize, size);
        QFETCH(QPoint, redAt);
        const QString file = path(u"orient%1.jpg"_s.arg(orientation));
        const ImageProbe probe = ImageDecoder::probe(file, ContentKind::Image);
        QVERIFY(probe.ok);
        QCOMPARE(probe.naturalSize, size);
        const DecodedImage img = ImageDecoder::decode(file, ContentKind::Image, QSize(), 0, true, makeCancelToken());
        QVERIFY(img.ok);
        QCOMPARE(img.image.size(), size);
        const QColor c = img.image.pixelColor(redAt);
        QVERIFY2(c.red() > 200 && c.blue() < 60, qPrintable(u"pixel %1,%2 is %3"_s.arg(redAt.x()).arg(redAt.y()).arg(c.name())));
    }

    void scaledDecodeOfLargeJpeg()
    {
        QElapsedTimer t;
        t.start();
        const DecodedImage img = ImageDecoder::decode(path(u"big.jpg"_s), ContentKind::Image, QSize(1200, 800), 0, true, makeCancelToken());
        QVERIFY(img.ok);
        QCOMPARE(img.naturalSize, QSize(6000, 4000));
        QVERIFY(!img.isFullResolution);
        QVERIFY(img.image.width() <= 1800);
        qInfo() << "6000x4000 JPEG display decode took" << t.elapsed() << "ms";
    }

    void fitAndZoomAtCursor()
    {
        ImageView view;
        view.resize(400, 300);
        view.setDecodeTarget(QSize(400, 300));
        view.show();
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        QVERIFY(view.isFitMode());
        QCOMPARE(view.zoomScale(), 1.0); // 400x200 fits at 100 %
        const QPointF anchor(300, 150);
        const QPointF before = (anchor - view.zoomOffset()) / view.zoomScale();
        QWheelEvent wheel(anchor, view.mapToGlobal(anchor), QPoint(), QPoint(0, 480), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&view, &wheel);
        QVERIFY(!view.isFitMode());
        QVERIFY(view.zoomScale() > 1.9 && view.zoomScale() < 2.1);
        const QPointF after = view.zoomOffset() + before * view.zoomScale();
        QVERIFY2((after - anchor).manhattanLength() < 1.0, "image point under the cursor must not move");

        // Zooming back to the fitted size snaps into fit mode.
        view.zoomAt(1.0, anchor);
        QVERIFY(view.isFitMode());
    }

    void panning()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        view.zoomAt(4.0, QPointF(200, 150));
        const QPointF start = view.zoomOffset();
        QTest::mousePress(&view, Qt::LeftButton, {}, QPoint(200, 150));
        QTest::mouseMove(&view, QPoint(150, 120));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, QPoint(150, 120));
        QCOMPARE(view.zoomOffset(), start + QPointF(-50, -30));
        // Cannot pan beyond the image edge.
        for (int i = 0; i < 20; ++i) {
            QTest::mousePress(&view, Qt::LeftButton, {}, QPoint(10, 10));
            QTest::mouseMove(&view, QPoint(390, 290));
            QTest::mouseRelease(&view, Qt::LeftButton, {}, QPoint(390, 290));
        }
        QCOMPARE(view.zoomOffset(), QPointF(0, 0));
    }

    void fullResolutionOnZoom()
    {
        ImageView view;
        view.resize(600, 400);
        view.setDecodeTarget(QSize(600, 400));
        view.show();
        QVERIFY(loadAndWait(view, u"big.jpg"_s));
        QVERIFY(!view.fullResolutionLoaded());
        QVERIFY(view.sourceImageSize().width() < 2000);
        view.zoomAt(1.0, QPointF(300, 200)); // 100 %: needs the real pixels
        QVERIFY(waitFor([&] {
            return view.fullResolutionLoaded();
        }));
        QVERIFY(waitFor([&] {
            return view.sourceImageSize() == QSize(6000, 4000);
        }));
    }

    void staleResultsAreIgnored()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        QSignalSpy ready(&view, &ContentView::ready);
        view.load(item(u"big.jpg"_s));
        view.load(item(u"wide.png"_s)); // switch before the first is decoded
        QVERIFY(waitFor([&] {
            return !ready.isEmpty();
        }));
        view.loader()->waitForIdle();
        QTest::qWait(50);
        QCOMPARE(view.naturalSize(), QSize(400, 200));
        QCOMPARE(view.sourceImageSize(), QSize(400, 200));
        QCOMPARE(ready.size(), 1);
    }

    void prefetchMakesSwitchInstant()
    {
        ImageView view;
        view.resize(400, 300);
        view.show();
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        view.prefetch({item(u"big.jpg"_s)});
        for (int i = 0; i < 4; ++i) { // probe, then decode
            view.loader()->waitForIdle();
            QTest::qWait(50);
        }
        QSignalSpy ready(&view, &ContentView::ready);
        view.load(item(u"big.jpg"_s));
        QCOMPARE(ready.size(), 1); // synchronously, from the cache
    }

    void rotationAndMirror()
    {
        ImageView view;
        view.resize(400, 400);
        view.show();
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        QCOMPARE(view.naturalSize(), QSize(400, 200));
        view.rotateLeft();
        QCOMPARE(view.rotation(), 270);
        QCOMPARE(view.naturalSize(), QSize(200, 400));
        view.toggleMirror();
        QVERIFY(view.isMirrored());
        // New file resets the view transform.
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        QCOMPARE(view.rotation(), 0);
    }

    void svgRendering()
    {
        ImageView view;
        view.resize(300, 300);
        view.show();
        QVERIFY(loadAndWait(view, u"vector.svg"_s));
        QCOMPARE(view.naturalSize(), QSize(120, 60));
        view.zoomAt(8.0, QPointF(150, 150));
        QVERIFY(waitFor([&] {
            return view.sourceImageSize().width() >= 900; // re-rendered sharp
        }));
    }

    void animatedGif()
    {
        if (!QFile::exists(path(u"anim.gif"_s))) {
            QSKIP("ImageMagick not installed");
        }
        ImageView view;
        view.resize(300, 200);
        view.show();
        QVERIFY(loadAndWait(view, u"anim.gif"_s));
        QVERIFY(view.isAnimated());
        const int start = view.animationFrameIndex();
        QTest::qWait(1000); // 200 ms per frame → about 5 frames
        const int advanced = view.animationFrameIndex() - start;
        QVERIFY2(advanced >= 4 && advanced <= 6, qPrintable(u"advanced %1 frames"_s.arg(advanced)));
        // Zoom works on animations too.
        view.zoomAt(2.0, QPointF(150, 100));
        QVERIFY(!view.isFitMode());
        // Switching to a still image stops the animation and frees frames.
        QVERIFY(loadAndWait(view, u"wide.png"_s));
        QVERIFY(!view.isAnimated());
        view.unload();
        QVERIFY(!view.hasImage());
    }

    void zeroDelayGifUsesBrowserDefault()
    {
        QCOMPARE(AnimationPlayer::effectiveDelay(0), 100);
        QCOMPARE(AnimationPlayer::effectiveDelay(10), 100);
        QCOMPARE(AnimationPlayer::effectiveDelay(40), 40);
        if (!QFile::exists(path(u"fast.gif"_s))) {
            QSKIP("ImageMagick not installed");
        }
        AnimationPlayer player;
        player.start(path(u"fast.gif"_s));
        QTest::qWait(1050);
        QVERIFY2(player.frameIndex() >= 8 && player.frameIndex() <= 12, qPrintable(QString::number(player.frameIndex())));
        player.stop();
        QVERIFY(player.currentFrame().isNull());
    }

    void multiPageTiff()
    {
        if (!QFile::exists(path(u"pages.tif"_s))) {
            QSKIP("ImageMagick not installed");
        }
        ImageView view;
        view.resize(200, 200);
        view.show();
        QVERIFY(loadAndWait(view, u"pages.tif"_s));
        QCOMPARE(view.pageCount(), 3);
        QKeyEvent next(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
        QVERIFY(view.handleKey(&next));
        QVERIFY(waitFor([&] {
            return view.currentPage() == 1 && view.hasImage();
        }));
    }

    void brokenFileFails()
    {
        ImageView view;
        view.show();
        QSignalSpy failed(&view, &ContentView::failed);
        view.load(item(u"broken.png"_s));
        QVERIFY(waitFor([&] {
            return !failed.isEmpty();
        }));
        QVERIFY(!failed.first().first().toString().isEmpty());
    }
};

QTEST_MAIN(ImageViewTest)
#include "imageviewtest.moc"
