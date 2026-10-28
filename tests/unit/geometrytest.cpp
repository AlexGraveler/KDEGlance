// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "geometrymath.h"

#include <QTest>

using namespace KDEGlance::Geometry;

class GeometryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void autoSizeSmallContentIsActualSize()
    {
        QCOMPARE(autoWindowSize(QSize(400, 300), QSize(1920, 1040), 0.7), QSize(400, 300));
    }
    void autoSizeLargeContentIsLimitedTo70Percent()
    {
        const QSize s = autoWindowSize(QSize(6000, 4000), QSize(1920, 1040), 0.7);
        QVERIFY(s.width() <= 1344 && s.height() <= 728);
        QCOMPARE(s, QSize(1092, 728)); // 3:2 kept
    }
    void autoSizeRespectsMinimum()
    {
        QCOMPARE(autoWindowSize(QSize(16, 16), QSize(1920, 1080), 0.7), MinimumWindowSize);
    }
    void autoSizeAt125Percent()
    {
        // 2560x1440 at 125 % = 2048x1152 logical; work area minus a 44 px panel.
        const QSize s = autoWindowSize(QSize(4000, 3000), QSize(2048, 1108), 0.7);
        QVERIFY(s.height() <= int(1108 * 0.7));
        QVERIFY(qAbs(double(s.width()) / s.height() - 4.0 / 3.0) < 0.01);
    }
    void clampShrinksOnly()
    {
        QCOMPARE(clampToArea(QSize(3000, 2000), QSize(1920, 1080)), QSize(1920, 1080));
        QCOMPARE(clampToArea(QSize(500, 400), QSize(1920, 1080)), QSize(500, 400));
    }

    void aspectLockRightEdge()
    {
        const QSize start(800, 400);
        QCOMPARE(aspectLockedSize(start, QSize(1000, 400), Qt::RightEdge), QSize(1000, 500));
        QCOMPARE(aspectLockedSize(start, QSize(600, 400), Qt::LeftEdge), QSize(600, 300));
    }
    void aspectLockBottomEdge()
    {
        QCOMPARE(aspectLockedSize(QSize(800, 400), QSize(800, 500), Qt::BottomEdge), QSize(1000, 500));
    }
    void aspectLockCornerFollowsLargerChange()
    {
        const QSize r = aspectLockedSize(QSize(800, 400), QSize(1200, 420), Qt::BottomEdge | Qt::RightEdge);
        QCOMPARE(r, QSize(1200, 600));
        const QSize r2 = aspectLockedSize(QSize(800, 400), QSize(810, 300), Qt::TopEdge | Qt::LeftEdge);
        QCOMPARE(r2, QSize(600, 300));
    }
    void aspectLockRespectsLimits()
    {
        const QSize r = aspectLockedSize(QSize(800, 400), QSize(100, 400), Qt::RightEdge, QSize(280, 200));
        QCOMPARE(r, QSize(400, 200));
        const QSize m = aspectLockedSize(QSize(800, 400), QSize(5000, 400), Qt::RightEdge, QSize(280, 200), QSize(1920, 1080));
        QCOMPARE(m, QSize(1920, 960));
    }

    void edgesHitTest()
    {
        const QSize w(400, 300);
        QCOMPARE(edgesAt(QPointF(2, 150), w, 6, 16), Qt::Edges(Qt::LeftEdge));
        QCOMPARE(edgesAt(QPointF(398, 150), w, 6, 16), Qt::Edges(Qt::RightEdge));
        QCOMPARE(edgesAt(QPointF(200, 1), w, 6, 16), Qt::Edges(Qt::TopEdge));
        QCOMPARE(edgesAt(QPointF(10, 298), w, 6, 16), Qt::BottomEdge | Qt::LeftEdge);
        QCOMPARE(edgesAt(QPointF(200, 150), w, 6, 16), Qt::Edges());
    }

    void centeredClamped()
    {
        const QRect area(0, 30, 1920, 1050);
        const QRect r = centeredIn(QSize(800, 600), area);
        QCOMPARE(r.center().x(), area.center().x());
        QVERIFY(area.contains(r));
        QCOMPARE(centeredIn(QSize(3000, 3000), area), area);
    }

    void zoomKeepsAnchorFixed()
    {
        const QSizeF image(4000, 3000);
        const QSizeF view(800, 600);
        ZoomState s = centered(image, view, fitScale(image, view));
        const QPointF anchor(600, 150);
        const QPointF imagePoint = (anchor - s.offset) / s.scale;
        const ZoomState z = zoomAt(s, image, view, s.scale * 3, anchor);
        const QPointF after = z.offset + imagePoint * z.scale;
        QVERIFY(qAbs(after.x() - anchor.x()) < 0.01);
        QVERIFY(qAbs(after.y() - anchor.y()) < 0.01);
    }
    void zoomOutCentersSmallImage()
    {
        const QSizeF image(400, 300);
        const QSizeF view(800, 600);
        const ZoomState z = zoomAt(centered(image, view, 1.0), image, view, 0.5, QPointF(10, 10));
        QCOMPARE(z.offset, QPointF(300, 225));
    }
    void panIsClamped()
    {
        const QSizeF image(1000, 1000);
        const QSizeF view(500, 500);
        ZoomState s{1.0, QPointF(100, 100)}; // would show empty space
        s = clampPan(s, image, view);
        QCOMPARE(s.offset, QPointF(0, 0));
        s.offset = QPointF(-900, -900);
        s = clampPan(s, image, view);
        QCOMPARE(s.offset, QPointF(-500, -500));
        QVERIFY(canPan(s, image, view));
        QVERIFY(!canPan(ZoomState{0.5, {}}, image, view));
    }
    void fitScaleNeverUpscalesByDefault()
    {
        QCOMPARE(fitScale(QSizeF(100, 100), QSizeF(800, 600)), 1.0);
        QCOMPARE(fitScale(QSizeF(1600, 1200), QSizeF(800, 600)), 0.5);
    }
};

QTEST_GUILESS_MAIN(GeometryTest)
#include "geometrytest.moc"
