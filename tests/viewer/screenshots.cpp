// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

// Renders the preview window for typical content into PNG files
// (screenshots/ in the build directory) for visual review.

#include "previewwindow.h"
#include "settings.h"
#include "settingsdialog.h"
#include "viewerapp.h"
#include "views/imageview.h"
#include "views/mediaview.h"

#include "../testutils.h"

#include <QDir>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class Screenshots : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString m_out;
    std::unique_ptr<Settings> m_settings;

    QString path(const QString &n) const
    {
        return m_dir.filePath(n);
    }
    void shoot(PreviewWindow &w, const QString &file, const QString &name, int waitMs = 600)
    {
        w.showItem(ViewerApp::makeItem(QUrl::fromLocalFile(path(file))), true);
        QVERIFY(waitFor([&] {
            return w.isVisible();
        }));
        QTest::qWait(waitMs);
        // Composite on a desktop-like background to show the rounded shape.
        const QImage window = w.grab().toImage();
        QImage canvas(window.size() + QSize(48, 48), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(0x5d, 0x6d, 0x7e));
        QPainter p(&canvas);
        p.drawImage(24, 24, window);
        p.end();
        QVERIFY(canvas.save(m_out + u'/' + name + u".png"_s));
        w.closePreview();
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_out = QDir::currentPath() + u"/kdeglance-screenshots"_s;
        QDir().mkpath(m_out);
        m_settings = std::make_unique<Settings>(path(u"settings.json"_s));
        m_settings->setMuted(true); // Screenshot capture should be silent.
        // A landscape picture similar to the reference: gradient sky, sun, hills.
        QImage img(1200, 800, QImage::Format_RGB32);
        {
            QPainter p(&img);
            QLinearGradient sky(0, 0, 0, 800);
            sky.setColorAt(0, QColor(0x3b, 0x3f, 0x9c));
            sky.setColorAt(1, QColor(0x4f, 0x9f, 0xc8));
            p.fillRect(img.rect(), sky);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(QColor(0xf3, 0xc8, 0x8b));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(900, 230), 120, 120);
            p.setBrush(QColor(0x26, 0x5c, 0x6a));
            p.drawPolygon(QPolygonF({QPointF(0, 800), QPointF(0, 560), QPointF(320, 250), QPointF(700, 800)}));
            p.setBrush(QColor(0x1b, 0x40, 0x56));
            p.drawPolygon(QPolygonF({QPointF(250, 800), QPointF(700, 330), QPointF(1200, 760), QPointF(1200, 800)}));
        }
        QVERIFY(img.save(path(u"landscape.jpg"_s)));
        QFile txt(path(u"report.odt"_s));
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("PK\x03\x04 fake");
        txt.close();
        if (!ffmpeg().isEmpty()) {
            run(ffmpeg(), {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"smptebars=size=640x480:rate=25"_s, u"-t"_s, u"5"_s, u"-c:v"_s,
                           u"mpeg4"_s, path(u"video.mkv"_s)});
            run(ffmpeg(), {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=12"_s, u"-i"_s, path(u"landscape.jpg"_s),
                           u"-map"_s, u"0:a"_s, u"-map"_s, u"1:v"_s, u"-c:v"_s, u"mjpeg"_s, u"-disposition:v"_s, u"attached_pic"_s, u"-metadata"_s,
                           u"title=Evening Mountains"_s, u"-metadata"_s, u"artist=KDEGlance Orchestra"_s, path(u"with-cover.mp3"_s)});
            run(ffmpeg(), {u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=12"_s, path(u"no-cover.ogg"_s)});
        }
    }

    void render()
    {
        PreviewWindow w(m_settings.get());
        w.setWorkArea(QRect(0, 0, 1280, 900));
        shoot(w, u"landscape.jpg"_s, u"01-image"_s);
        // Hovered controls and zoomed in.
        w.showItem(ViewerApp::makeItem(QUrl::fromLocalFile(path(u"landscape.jpg"_s))), true);
        QVERIFY(waitFor([&] {
            return w.imageView()->hasImage();
        }));
        QTest::mouseMove(w.imageView(), QPoint(w.width() / 2, w.height() / 2));
        QEnterEvent enter(QPointF(10, 10), QPointF(10, 10), QPointF(10, 10));
        QApplication::sendEvent(w.imageView(), &enter);
        w.imageView()->zoomAt(1.6, QPointF(w.width() * 0.7, w.height() * 0.3));
        QTest::qWait(400);
        QVERIFY(w.grab().save(m_out + u"/02-image-zoom-controls.png"_s));
        w.closePreview();
        if (QFile::exists(path(u"with-cover.mp3"_s))) {
            shoot(w, u"with-cover.mp3"_s, u"03-audio-cover"_s, 900);
            shoot(w, u"no-cover.ogg"_s, u"04-audio-placeholder"_s, 900);
            shoot(w, u"video.mkv"_s, u"05-video"_s, 900);
        }
        shoot(w, u"report.odt"_s, u"06-other-file"_s);
        QFile broken(path(u"broken.png"_s));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("\x89PNG\r\n\x1a\nxx");
        broken.close();
        shoot(w, u"broken.png"_s, u"07-error"_s);
        SettingsDialog dialog(m_settings.get());
        dialog.show();
        QTest::qWait(200);
        QVERIFY(dialog.grab().save(m_out + u"/08-settings.png"_s));
        qInfo() << "screenshots written to" << m_out;
    }
};

QTEST_MAIN(Screenshots)
#include "screenshots.moc"
