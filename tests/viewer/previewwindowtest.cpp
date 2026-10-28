// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "geometrymath.h"
#include "previewwindow.h"
#include "settings.h"
#include "viewerapp.h"
#include "views/imageview.h"
#include "views/infoview.h"
#include "views/mediaview.h"
#include "views/textview.h"

#include "../testutils.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <cmath>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class PreviewWindowTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<PreviewWindow> m_window;

    QString path(const QString &name) const
    {
        return m_dir.filePath(name);
    }
    void open(const QString &name, bool newSession = true)
    {
        m_window->showItem(ViewerApp::makeItem(QUrl::fromLocalFile(path(name))), newSession);
        QVERIFY(waitFor([&] {
            return m_window->isVisible();
        }));
        QTest::qWait(50);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(twoColorImage(8, 4).save(path(u"tiny.png"_s)));
        QVERIFY(twoColorImage(400, 200).save(path(u"wide.png"_s)));
        QVERIFY(twoColorImage(3000, 1000).save(path(u"huge.png"_s)));
        QVERIFY(twoColorImage(300, 600).save(path(u"tall.png"_s)));
        QFile txt(path(u"notes.txt"_s));
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("hello");
    }

    void init()
    {
        QFile::remove(path(u"settings.json"_s));
        m_settings = std::make_unique<Settings>(path(u"settings.json"_s));
        m_window = std::make_unique<PreviewWindow>(m_settings.get());
        m_window->setWorkArea(QRect(0, 0, 1600, 1000));
    }
    void cleanup()
    {
        m_window.reset();
    }

    void opensWithContentSize()
    {
        open(u"wide.png"_s);
        QCOMPARE(m_window->currentView(), m_window->imageView());
        QCOMPARE(m_window->size(), QSize(400, 200).expandedTo(Geometry::MinimumWindowSize));
    }

    void largeImageFitsAvailableWorkArea()
    {
        open(u"huge.png"_s);
        QVERIFY(waitFor([&] {
            return m_window->width() > 400;
        }));
        QVERIFY(m_window->width() <= 1600);
        QVERIFY(m_window->height() <= 1000);
        QCOMPARE(m_window->size(), QSize(1600, 533));
    }

    void imageGeometryPersistsButZoomDoesNot()
    {
        open(u"wide.png"_s);
        QVERIFY(waitFor([&] { return m_window->imageView()->hasImage(); }));
        m_window->beginInteractiveResize(Qt::RightEdge | Qt::BottomEdge, false);
        m_window->resize(600, 400);
        m_window->endInteractiveResize();
        m_window->imageView()->zoomAt(3, QPointF(200,100));
        QCOMPARE(m_window->size(), QSize(600,400));
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(600,400)));
        QVERIFY(m_settings->save());
        m_window->closePreview();
        QCOMPARE(m_window->imageView()->zoomScale(),1.0);
        m_window.reset();
        m_settings = std::make_unique<Settings>(path(u"settings.json"_s));
        m_settings->load();
        m_window = std::make_unique<PreviewWindow>(m_settings.get());
        m_window->setWorkArea(QRect(0,0,1600,1000));
        open(u"tall.png"_s);
        QVERIFY(waitFor([&] { return m_window->imageView()->hasImage(); }));
        QCOMPARE(m_window->size(), QSize(600,400));
        QVERIFY(m_window->imageView()->isFitMode());
        QVERIFY(qAbs(m_window->imageView()->zoomScale()-2.0/3)<0.001);
        m_window->setWorkArea(QRect(0,0,500,300));
        QVERIFY(m_window->width()<=500 && m_window->height()<=300);
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(600,400)));
    }

    void wheelResizesPhotoAndWindowWithoutSaving()
    {
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        auto* view=m_window->imageView();
        const auto wheel=[&](int delta) {
            const QPointF pos(view->rect().center());
            QWheelEvent event(pos,view->mapToGlobal(pos.toPoint()),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(view,&event);
        };
        wheel(-120); QVERIFY(m_window->width()<400 && m_window->height()<200);
        QVERIFY(qAbs(double(m_window->width())/m_window->height()-2)<.02);
        QVERIFY(view->displayRect().left()<1 && view->displayRect().top()<1);
        wheel(120); QVERIFY(m_window->width()>330);
        wheel(2400); QVERIFY(m_window->width()<=1600 && m_window->height()<=1000);
        QVERIFY(view->displayRect().left()<1 && view->displayRect().top()<1);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
        m_window->closePreview(); open(u"wide.png"_s);
        QCOMPARE(m_window->size(),QSize(400,200));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false); m_window->resize(600,600); m_window->endInteractiveResize();
        wheel(-120); QVERIFY(m_window->size()!=QSize(600,600));
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(600,600)));
        m_window->closePreview(); open(u"wide.png"_s); QCOMPARE(m_window->size(),QSize(600,600));
        QVERIFY(view->isFitMode()); QCOMPARE(view->displayRect(),QRectF(0,150,600,300));
    }
    void wheelContinuesPastScreenAndPreservesCursor()
    {
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        auto* view=m_window->imageView();
        const auto wheel=[&](int delta,QPointF p) {
            QWheelEvent e(p,view->mapToGlobal(p.toPoint()),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(view,&e);
        };
        wheel(960,QPointF(200,100)); // 4x: maximum 1600x800 window
        const auto capped=m_window->size(); const auto scale=view->zoomScale();
        const QPointF cursor(1000,500), point=(cursor-view->zoomOffset())/scale;
        wheel(120,cursor);
        QCOMPARE(m_window->size(),capped); QVERIFY(view->zoomScale()>scale);
        QVERIFY((view->zoomOffset()+point*view->zoomScale()-cursor).manhattanLength()<.01);
        const auto enlarged=view->zoomScale();
        wheel(-120,cursor); QCOMPARE(m_window->size(),capped);
        QVERIFY(view->zoomScale()<enlarged);
        wheel(-120,cursor); QVERIFY(m_window->width()<capped.width());
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
    }

    void resetReopenMoveThenWheelKeepsLocalAnchor()
    {
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(650,400);m_window->endInteractiveResize();
        QTest::mouseClick(m_window->imageView(),Qt::MiddleButton,{},QPoint(200,100));
        QTest::mouseDClick(m_window->imageView(),Qt::MiddleButton,{},QPoint(200,100));
        QTest::mouseRelease(m_window->imageView(),Qt::MiddleButton,{},QPoint(200,100));
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
        QCOMPARE(m_window->size(),QSize(400,200));
        m_window->closePreview();open(u"wide.png"_s);
        auto* view=m_window->imageView();
        const auto local=view->zoomOffset(); const auto scale=view->zoomScale();
        m_window->move(310,220); QCOMPARE(view->zoomOffset(),local); QCOMPARE(view->zoomScale(),scale);
        const auto wheel=[&](int delta,QPointF p) {
            QWheelEvent e(p,view->mapToGlobal(p.toPoint()),{},QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(view,&e);
        };
        wheel(1440,QPointF(200,100));
        const QPointF p(900,400),point=(p-view->zoomOffset())/view->zoomScale();
        const auto size=m_window->size(); const auto oldScale=view->zoomScale();
        wheel(120,p);
        QVERIFY((view->zoomOffset()+point*view->zoomScale()-p).manhattanLength()<.01);
        for(int i=0;i<40;++i) {wheel(-15,p);wheel(15,p);}
        QCOMPARE(m_window->size(),size);
        QVERIFY(qAbs(view->zoomScale()-oldScale*std::pow(2.,.25))<1e-8);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
    }
    void highPrecisionWheelIsReversibleWithinCrop()
    {
        open(u"wide.png"_s);auto* view=m_window->imageView();
        QVERIFY(waitFor([&]{return view->hasImage();}));
        view->zoomAt(12,QPointF(200,100));
        m_window->setPhotoCompositorManaged(true); // fixed viewport
        const auto before=view->displayRect(); const auto size=m_window->size();
        const QPointF p(210,110);
        for(int i=0;i<40;++i) {
            for(int d:{3,-3}) {
                QWheelEvent e(p,view->mapToGlobal(p.toPoint()),QPoint(0,d),{},Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
                QApplication::sendEvent(view,&e);
            }
        }
        QCOMPARE(m_window->size(),size);
        QVERIFY((view->zoomOffset()-before.topLeft()).manhattanLength()<1e-8);
        QVERIFY(qAbs(view->displayRect().width()-before.width())<1e-8);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
    }
    void compositorGeometryCannotReplaceManualSize()
    {
        open(u"wide.png"_s);
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(900,600);m_window->endInteractiveResize();
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->setPhotoCompositorManaged(true);
        m_window->resize(800,1000);m_window->endInteractiveResize();
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(900,600)));
        m_window->setPhotoCompositorManaged(false);
        m_window->closePreview();open(u"wide.png"_s);
        QCOMPARE(m_window->size(),QSize(900,600));
    }
    void reopenMaximizedPhotoRestoresManualGeometry()
    {
        open(u"wide.png"_s);
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(900,600);m_window->endInteractiveResize();
        m_window->showMaximized();
        m_window->setPhotoCompositorManaged(true);
        m_window->closePreview();open(u"wide.png"_s);
        QVERIFY(!m_window->isMaximized());QCOMPARE(m_window->size(),QSize(900,600));
        QVERIFY(m_window->imageView()->isFitMode());
    }
    void zoomOutOfCropKeepsWindowUntilImageFits()
    {
        open(u"wide.png"_s); auto* view=m_window->imageView();
        QVERIFY(waitFor([&]{return view->hasImage();}));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(600,600);m_window->endInteractiveResize();
        view->zoomAt(8,QPointF(300,300));
        QWheelEvent e(QPointF(300,300),view->mapToGlobal(QPoint(300,300)),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(view,&e);QCOMPARE(m_window->size(),QSize(600,600));
        QVERIFY(view->zoomScale()<8 && view->displayRect().width()>600);
    }

    void wheelAfterCompositorResizeDoesNotSaveTemporarySize()
    {
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false); m_window->resize(600,600);
        QTest::qWait(120);
        auto* view=m_window->imageView(); const QPointF p(300,300);
        QWheelEvent wheel(p,view->mapToGlobal(p.toPoint()),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(view,&wheel);
        QVERIFY(m_window->size()!=QSize(600,600));
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(600,600)));
    }
    void freeResizeFitsWithoutDistortionAndAltUsesPhotoRatio()
    {
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        auto* view=m_window->imageView(); view->zoomAt(8,QPointF(200,100));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false); m_window->resize(600,600); m_window->endInteractiveResize();
        QCOMPARE(m_window->size(),QSize(600,600)); QCOMPARE(view->displayRect(),QRectF(0,150,600,300));
        QCOMPARE(view->grab().toImage().pixelColor(300,40),QColor(Qt::black));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,true); m_window->resize(800,650); m_window->endInteractiveResize();
        QCOMPARE(m_window->size(),QSize(800,400)); QCOMPARE(view->displayRect(),QRectF(0,0,800,400));
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(800,400)));
        m_window->closePreview(); open(u"wide.png"_s); QCOMPARE(m_window->size(),QSize(800,400));
    }
    void tinyPhotoStillFillsManualProportionalWindow()
    {
        open(u"tiny.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,true); m_window->resize(800,650); m_window->endInteractiveResize();
        QCOMPARE(m_window->size(),QSize(1300,650));
        QCOMPARE(m_window->imageView()->displayRect(),QRectF(0,0,1300,650));
        const QPointF p(500,300); QWheelEvent wheel(p,m_window->imageView()->mapToGlobal(p.toPoint()),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(m_window->imageView(),&wheel);
        QVERIFY(m_window->width()>1000 && m_window->width()<1300);
        QVERIFY(m_window->imageView()->displayRect().left()<1 && m_window->imageView()->displayRect().top()<1);
        QCOMPARE(m_settings->savedSize(SizeCategory::Image),std::optional<QSize>(QSize(1300,650)));
    }
    void smallManualPhotoSizePersists()
    {
        open(u"wide.png"_s); m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(90,70); m_window->endInteractiveResize();
        QVERIFY(m_settings->save()); m_settings->load(); m_window->closePreview(); open(u"wide.png"_s);
        QCOMPARE(m_window->size(),QSize(90,70));
    }

    void spaceAndEscapeClose()
    {
        for (int key : {int(Qt::Key_Space), int(Qt::Key_Escape)}) {
            open(u"wide.png"_s);
            QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
            QTest::keyClick(m_window.get(), Qt::Key(key));
            QCOMPARE(closed.size(), 1);
            QVERIFY(!m_window->isVisible());
            QVERIFY(!m_window->isPreviewVisible());
        }
    }

    void customHotkeyAlsoCloses()
    {
        m_settings->setShortcut(QKeySequence(u"Ctrl+Y"_s));
        open(u"wide.png"_s);
        QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
        QTest::keyClick(m_window.get(), Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(closed.size(), 1);
    }

    void arrowsRequestNavigation()
    {
        open(u"wide.png"_s);
        QSignalSpy nav(m_window.get(), &PreviewWindow::navigateRequested);
        QTest::keyClick(m_window.get(), Qt::Key_Right);
        QTest::keyClick(m_window.get(), Qt::Key_Left);
        QCOMPARE(nav.size(), 2);
        QCOMPARE(nav.at(0).first().toInt(), 1);
        QCOMPARE(nav.at(1).first().toInt(), -1);
        QVERIFY(m_window->isVisible()); // window stays, only content changes
    }

    void switchingKeepsWindow()
    {
        open(u"wide.png"_s);
        const WId id = m_window->winId();
        open(u"tall.png"_s, false);
        QCOMPARE(m_window->winId(), id);
        QCOMPARE(m_window->currentView(), m_window->imageView());
    }

    void imageGeometrySurvivesMixedNavigation()
    {
        open(u"wide.png"_s);
        m_window->beginInteractiveResize(Qt::RightEdge|Qt::BottomEdge,false);
        m_window->resize(777,555); m_window->endInteractiveResize();
        open(u"notes.txt"_s,false);
        QVERIFY(m_window->size()!=QSize(777,555));
        open(u"tall.png"_s,false);
        QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        QCOMPARE(m_window->size(),QSize(777,555));
    }
    void legacyPersistentZoomIsIgnored()
    {
        m_window.reset();
        QFile file(path(u"settings.json"_s)); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schema":1,"image":{"zoom":8},"window":{"sizes":{"image":{"w":600,"h":400}}}})"); file.close();
        m_settings=std::make_unique<Settings>(path(u"settings.json"_s));
        QCOMPARE(m_settings->load(),Settings::LoadStatus::Migrated);
        QVERIFY(!m_settings->toJson().value(u"image"_s).toObject().contains(u"zoom"_s));
        m_window=std::make_unique<PreviewWindow>(m_settings.get()); m_window->setWorkArea(QRect(0,0,1600,1000));
        open(u"wide.png"_s); QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        QCOMPARE(m_window->size(),QSize(600,400)); QCOMPARE(m_window->imageView()->zoomScale(),1.5);
    }
    void imageZoomDoesNotResizeOrSaveWindow()
    {
        open(u"wide.png"_s);
        QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        const auto size=m_window->size();
        for(double scale : {0.5,1.0,8.0}) {
            m_window->imageView()->zoomAt(scale,QPointF(200,100));
            QCOMPARE(m_window->size(),size);
            QVERIFY(!m_settings->savedSize(SizeCategory::Image));
        }
        m_window->closePreview(); open(u"wide.png"_s);
        QVERIFY(waitFor([&]{return m_window->imageView()->hasImage();}));
        QCOMPARE(m_window->imageView()->zoomScale(),1.0);
    }

    void freeResize()
    {
        open(u"wide.png"_s);
        const QSize start = m_window->size();
        m_window->beginInteractiveResize(Qt::RightEdge, false);
        m_window->resize(start.width() + 200, start.height() + 30);
        QCOMPARE(m_window->size(), QSize(start.width() + 200, start.height() + 30));
        m_window->endInteractiveResize();
    }

    void altResizeKeepsAspectRatio()
    {
        open(u"wide.png"_s);
        m_window->resize(600, 300);
        const QSize start = m_window->size();
        m_window->beginInteractiveResize(Qt::RightEdge, true);
        QVERIFY(m_window->aspectLockActive());
        // The compositor proposes a new width with the old height...
        m_window->resize(800, 300);
        // ...the window answers with the ratio of the moment the drag began.
        QCOMPARE(m_window->size(), QSize(800, 400));
        m_window->resize(1000, 380);
        QCOMPARE(m_window->size(), QSize(1000, 500));
        m_window->endInteractiveResize();
        QVERIFY(!m_window->aspectLockActive());
        Q_UNUSED(start)
        // Corner drag: the larger relative change drives.
        m_window->beginInteractiveResize(Qt::BottomEdge | Qt::LeftEdge, true);
        m_window->resize(1010, 700);
        QCOMPARE(m_window->size(), QSize(1400, 700));
        m_window->endInteractiveResize();
    }

    void userSizeIsRememberedPerCategory()
    {
        open(u"wide.png"_s);
        m_window->beginInteractiveResize(Qt::BottomEdge | Qt::RightEdge, false);
        m_window->resize(777, 555);
        m_window->endInteractiveResize();
        QCOMPARE(m_settings->savedSize(SizeCategory::Image), std::optional<QSize>(QSize(777, 555)));
        QVERIFY(!m_settings->savedSize(SizeCategory::Video));
        QVERIFY(!m_settings->savedSize(SizeCategory::Audio));
        // Restore user geometry independently of the next image aspect ratio.
        m_window->closePreview();
        open(u"tall.png"_s);
        QCOMPARE(m_window->size(), QSize(777, 555));
    }

    void programmaticSizesAreNotSaved()
    {
        open(u"wide.png"_s);
        open(u"tall.png"_s, false);
        open(u"huge.png"_s, false);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
    }

    void savedSizeClampedToWorkArea()
    {
        m_settings->setSavedSize(SizeCategory::Text, QSize(3000, 2000));
        open(u"notes.txt"_s);
        QCOMPARE(m_window->size(), QSize(1600, 1000));
        // Resolution change while open shrinks the window.
        m_window->setWorkArea(QRect(0, 0, 1280, 700));
        QCOMPARE(m_window->size(), QSize(1280, 700));
    }

    void singleMiddleClickCloses()
    {
        open(u"wide.png"_s);
        QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
        QTest::mouseClick(m_window->imageView(), Qt::MiddleButton, {}, QPoint(10, 10));
        QCOMPARE(closed.size(), 0); // waits for a possible second click
        QVERIFY(waitFor([&] {
            return closed.size() == 1;
        }, QApplication::doubleClickInterval() + 1000));
    }

    void doubleMiddleClickInCenterResetsSettings()
    {
        m_settings->setSavedSize(SizeCategory::Image, QSize(900, 600));
        m_settings->setSyncSelection(false);
        open(u"wide.png"_s);
        m_window->imageView()->rotateLeft();
        m_window->imageView()->toggleMirror();
        QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
        QSignalSpy reset(m_window.get(), &PreviewWindow::settingsResetRequested);
        const QPoint center = m_window->rect().center();
        QTest::mouseClick(m_window->imageView(), Qt::MiddleButton, {}, center);
        QTest::mouseClick(m_window->imageView(), Qt::MiddleButton, {}, center + QPoint(3, 2));
        QCOMPARE(reset.size(), 1);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
        QVERIFY(!m_settings->syncSelection()); // global settings are untouched
        QCOMPARE(m_window->imageView()->rotation(),0);
        QVERIFY(!m_window->imageView()->isMirrored());
        QTest::qWait(QApplication::doubleClickInterval() + 200);
        QCOMPARE(closed.size(), 0); // a reset does not close the preview
        QVERIFY(m_window->isVisible());
    }

    void doubleMiddleClickAtEdgeResetsOnlyActiveModule()
    {
        m_settings->setSavedSize(SizeCategory::Image, QSize(900, 600));
        open(u"wide.png"_s);
        QSignalSpy reset(m_window.get(), &PreviewWindow::settingsResetRequested);
        QTest::mouseClick(m_window->imageView(), Qt::MiddleButton, {}, QPoint(20, 20));
        QTest::mouseClick(m_window->imageView(), Qt::MiddleButton, {}, QPoint(22, 20));
        QCOMPARE(reset.size(), 1);
        QVERIFY(!m_settings->savedSize(SizeCategory::Image));
    }

    void focusLossCloses()
    {
        open(u"wide.png"_s);
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
        QWidget other;
        other.resize(200, 200);
        other.show();
        other.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QVERIFY(waitFor([&] {
            return closed.size() == 1;
        }));
    }

    void ownControlsDoNotClose()
    {
        open(u"wide.png"_s);
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QSignalSpy closed(m_window.get(), &PreviewWindow::closed);
        // Clicking the overlay buttons keeps the window open.
        const auto buttons = m_window->imageView()->findChildren<QAbstractButton *>();
        QVERIFY(!buttons.isEmpty());
        for (QAbstractButton *b : buttons) {
            b->show();
            QTest::mouseClick(b, Qt::LeftButton);
        }
        QTest::qWait(100);
        QCOMPARE(closed.size(), 0);
    }

    void focusLossCanBeDisabled()
    {
        m_settings->setCloseOnFocusLoss(false);
        open(u"wide.png"_s);
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        QWidget other;
        other.show();
        other.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QTest::qWait(100);
        QVERIFY(m_window->isPreviewVisible());
    }

    void textFilesOpenInTextViewer()
    {
        open(u"notes.txt"_s);
        QCOMPARE(m_window->currentView(), m_window->textView());
        QCOMPARE(m_window->currentCategory(), SizeCategory::Text);
    }

    void unsupportedBinaryStillShowsInfoCard()
    {
        QFile binary(path(u"opaque.bin"_s));QVERIFY(binary.open(QIODevice::WriteOnly));binary.write(QByteArray::fromHex("00010203ff00fe00fd00fc00"));binary.close();
        open(u"opaque.bin"_s);QCOMPARE(m_window->currentView(),m_window->infoView());QCOMPARE(m_window->infoView()->headline(),u"opaque.bin"_s);
    }

    void deletedFileShowsMessage()
    {
        QFile::copy(path(u"wide.png"_s), path(u"gone.png"_s));
        const PreviewItem item = ViewerApp::makeItem(QUrl::fromLocalFile(path(u"gone.png"_s)));
        QFile::remove(path(u"gone.png"_s));
        m_window->showItem(item, true);
        QVERIFY(waitFor([&] {
            return m_window->isVisible() && m_window->currentView() == m_window->infoView();
        }));
        QVERIFY(!m_window->infoView()->message().isEmpty());
    }

    void repeatedOpenCloseCycles()
    {
        QTest::failOnWarning("Cannot set the target window on a shadow that already has native platform resources allocated. To do so, destroy() the shadow and then setWindow() and create()");
        for (int i = 0; i < 60; ++i) {
            open(i % 2 ? u"wide.png"_s : u"tall.png"_s);
            QTest::keyClick(m_window.get(), Qt::Key_Escape);
            QVERIFY(!m_window->isVisible());
        }
    }
};

QTEST_MAIN(PreviewWindowTest)
#include "previewwindowtest.moc"
