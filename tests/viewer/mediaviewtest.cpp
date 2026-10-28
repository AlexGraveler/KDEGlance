// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "settings.h"
#include "viewerapp.h"
#include "views/mediacontrols.h"
#include "views/mediaview.h"

#include "../testutils.h"

#include <QSignalSpy>
#include <QAbstractButton>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <QPainter>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class MediaViewTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    std::unique_ptr<Settings> m_settings;
    QString path(const QString &name) const
    {
        return m_dir.filePath(name);
    }
    bool ff(const QStringList &args)
    {
        QStringList a{u"-loglevel"_s, u"error"_s, u"-y"_s};
        a << args;
        return run(ffmpeg(), a);
    }
    bool loadAndWait(MediaView &view, const QString &name, QString *error = nullptr)
    {
        QSignalSpy ready(&view, &ContentView::ready);
        QSignalSpy failed(&view, &ContentView::failed);
        view.load(ViewerApp::makeItem(QUrl::fromLocalFile(path(name))));
        const bool done = waitFor([&] {
            return !ready.isEmpty() || !failed.isEmpty();
        }, 20000);
        if (error && !failed.isEmpty()) {
            *error = failed.first().first().toString();
        }
        return done && failed.isEmpty();
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (ffmpeg().isEmpty()) {
            QSKIP("ffmpeg is needed to generate media samples");
        }
        m_settings = std::make_unique<Settings>(path(u"settings.json"_s));
        QVERIFY(ff({u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=320x180:rate=25"_s, u"-t"_s, u"3"_s, u"-pix_fmt"_s, u"yuv420p"_s, u"-c:v"_s, u"mpeg4"_s, path(u"clip.mkv"_s)}));
        // Portrait phone video: 320x180 stored, rotated 90° by metadata.
        QVERIFY(ff({u"-f"_s, u"lavfi"_s, u"-i"_s, u"testsrc=size=320x180:rate=25"_s, u"-t"_s, u"1"_s, u"-c:v"_s, u"mpeg4"_s, path(u"landscape.mp4"_s)}));
        QVERIFY(ff({u"-display_rotation"_s, u"90"_s, u"-i"_s, path(u"landscape.mp4"_s), u"-c"_s, u"copy"_s, path(u"portrait.mp4"_s)}));
        // Audio with tags and an embedded cover.
        QImage cover(64, 64, QImage::Format_RGB32);
        cover.fill(Qt::magenta);
        QVERIFY(cover.save(path(u"embedded-art.png"_s)));
        QVERIFY(ff({u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=frequency=330:duration=3"_s, u"-i"_s, path(u"embedded-art.png"_s), u"-map"_s, u"0:a"_s, u"-map"_s, u"1:v"_s,
                    u"-c:v"_s, u"png"_s, u"-disposition:v"_s, u"attached_pic"_s, u"-metadata"_s, u"title=Cover Song"_s, u"-metadata"_s, u"artist=Test Artist"_s,
                    u"-c:a"_s, u"flac"_s, path(u"tagged.flac"_s)}));
        QVERIFY(ff({u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=2"_s, path(u"plain.wav"_s)}));
        // Audio-only Matroska: must get the audio layout although .mkv suggests video.
        QVERIFY(ff({u"-f"_s, u"lavfi"_s, u"-i"_s, u"sine=duration=2"_s, u"-c:a"_s, u"flac"_s, path(u"audioonly.mkv"_s)}));
        QFile midi(path(u"note.mid"_s));
        QVERIFY(midi.open(QIODevice::WriteOnly));
        midi.write(QByteArray::fromHex("4d546864000000060000000100604d54726b0000000c00903c4060803c0000ff2f00"));
        midi.close();
        QFile broken(path(u"broken.mp4"_s));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write(QByteArray(4096, 'x'));
    }

    void missingMidiDoesNotCancelNextFile()
    {
        // Force the missing-synthesizer path even on machines with SoundFonts.
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", "/nonexistent-kdeglance-test");
        MediaView view(m_settings.get());
        view.show();
        QSignalSpy failed(&view, &ContentView::failed);
        view.load(ViewerApp::makeItem(QUrl::fromLocalFile(path(u"note.mid"_s))));
        qputenv("PATH", oldPath);
        QVERIFY(loadAndWait(view, u"plain.wav"_s));
        QVERIFY(failed.isEmpty());
        QCOMPARE(view.player()->source(), QUrl::fromLocalFile(path(u"plain.wav"_s)));
    }

    void missingMidiReportsDependency()
    {
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", "/nonexistent-kdeglance-test");
        MediaView view(m_settings.get());
        QSignalSpy failed(&view, &ContentView::failed);
        view.load(ViewerApp::makeItem(QUrl::fromLocalFile(path(u"note.mid"_s))));
        qputenv("PATH", oldPath);
        QVERIFY(waitFor([&] { return !failed.isEmpty(); }));
        QVERIFY(failed.first().first().toString().contains(u"fluidsynth"_s));
        QVERIFY(!view.isLoaded());
    }

    void seekWheelDoesNotChangeVolume()
    {
        MediaControls controls;controls.resize(600,100);controls.setDuration(3000);controls.setPosition(2500);controls.setSeekable(true);controls.show();
        QSignalSpy seek(&controls,&MediaControls::seekRequested),volume(&controls,&MediaControls::volumeRequested);auto *slider=controls.seekSlider();
        const QPointF pos(slider->rect().center());QWheelEvent up(pos,slider->mapToGlobal(pos.toPoint()),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(slider,&up);QCOMPARE(seek.last().first().toLongLong(),3000);QVERIFY(volume.isEmpty());
        QWheelEvent down(pos,slider->mapToGlobal(pos.toPoint()),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);QApplication::sendEvent(slider,&down);QCOMPARE(seek.last().first().toLongLong(),2000);
        controls.setPosition(0);QApplication::sendEvent(slider,&down);QCOMPARE(seek.last().first().toLongLong(),0);
    }
    void videoSurfaceClickNavigation()
    {
        MediaView view(m_settings.get()); view.resize(640,360); view.show();
        QVERIFY(loadAndWait(view,u"clip.mkv"_s));
        QVERIFY(waitFor([&]{ return view.surface()->hasFrame(); }));
        QSignalSpy nav(&view,&ContentView::navigateRequested);
        QSignalSpy close(&view,&ContentView::closeRequested);
        const auto image = view.surface()->imageRect();
        QTest::mouseClick(view.surface(),Qt::LeftButton,{},QPoint(image.left()+image.width()*.1,image.center().y()));
        QVERIFY(waitFor([&]{return nav.size()==1;})); QCOMPARE(nav.last().first().toInt(),-1);
        QTest::mouseClick(view.surface(),Qt::LeftButton,{},QPoint(image.left()+image.width()*.9,image.center().y()));
        QVERIFY(waitFor([&]{return nav.size()==2;})); QCOMPARE(nav.last().first().toInt(),1);
        view.controls()->rotateButton()->click(); QTest::qWait(QApplication::doubleClickInterval()+20);
        QCOMPARE(nav.size(),2); QVERIFY(close.isEmpty());
    }
    void videoRotationReallyTurnsCounterclockwise()
    {
        QImage frame(80,40,QImage::Format_RGB32);
        { QPainter p(&frame); p.fillRect(0,0,40,20,Qt::red); p.fillRect(40,0,40,20,Qt::green);
          p.fillRect(0,20,40,20,Qt::blue); p.fillRect(40,20,40,20,Qt::yellow); }
        VideoSurface surface; surface.resize(400,400); surface.show(); surface.setFrame(QVideoFrame(frame));
        QVERIFY(waitFor([&]{return surface.hasFrame();}));
        const QColor topLeft[] = {Qt::red,Qt::green,Qt::yellow,Qt::blue,Qt::red};
        for(int i=0;i<=4;++i) {
            if(i) surface.rotateLeft();
            const auto image=surface.grab().toImage(); const auto rect=surface.imageRect();
            QCOMPARE(image.pixelColor(QPoint(rect.left()+rect.width()*.25,rect.top()+rect.height()*.25)),topLeft[i]);
        }
    }
    void manualVideoRotationAndZoom()
    {
        MediaView view(m_settings.get());view.resize(640,360);view.show();QVERIFY(loadAndWait(view,u"clip.mkv"_s));QVERIFY(waitFor([&]{return view.surface()->frameSize()==QSize(320,180);}));
        view.controls()->rotateButton()->click();QVERIFY(waitFor([&]{return view.surface()->frameSize()==QSize(180,320);}));QCOMPARE(view.surface()->rotation(),270);
        view.surface()->zoomAt(2,QPointF(320,180));QCOMPARE(view.surface()->zoom(),2.0);
        for(int i=2;i<=4;++i) {
            view.controls()->rotateButton()->click();
            QVERIFY(waitFor([&]{return view.surface()->frameSize()==(i%2?QSize(180,320):QSize(320,180));}));
            QCOMPARE(view.surface()->rotation(),(i*270)%360);
            QCOMPARE(view.surface()->zoom(),2.0);
        }
        view.unload();QCOMPARE(view.player()->playbackState(),QMediaPlayer::StoppedState);QVERIFY(view.player()->source().isEmpty());
    }

    void moduleResetDoesNotChangeOtherMediaSettings()
    {
        Settings scoped(path(u"independent-reset.json"_s));
        auto* settings=&scoped;
        settings->setModuleVolume(SizeCategory::Audio,.25); settings->setModuleVolume(SizeCategory::Video,.6);
        settings->setSavedSize(SizeCategory::Audio,QSize(510,520));
        MediaView view(settings); view.resize(640,360); view.show(); QVERIFY(loadAndWait(view,u"clip.mkv"_s));
        QVERIFY(waitFor([&]{return view.surface()->hasFrame();}));
        view.surface()->rotateLeft(); view.surface()->zoomAt(2,QPointF(320,180));
        settings->resetModule(SizeCategory::Video); view.resetPresentation();
        QCOMPARE(view.surface()->rotation(),0); QCOMPARE(view.surface()->zoom(),1.0);
        QCOMPARE(settings->moduleVolume(SizeCategory::Audio),.25); QCOMPARE(settings->moduleVolume(SizeCategory::Video),.8);
        QCOMPARE(settings->savedSize(SizeCategory::Audio),std::optional<QSize>(QSize(510,520)));
        view.unload();
    }
    void videoPlaysWithAspect()
    {
        MediaView view(m_settings.get());
        view.resize(640, 360);
        view.show();
        QString err;
        QVERIFY2(loadAndWait(view, u"clip.mkv"_s, &err), qPrintable(err));
        QVERIFY(view.hasVideo());
        QCOMPARE(view.sizeCategory(), SizeCategory::Video);
        QVERIFY(waitFor([&] {
            return view.surface()->hasFrame() && view.surface()->framesShown() > 2;
        }));
        QCOMPARE(view.surface()->frameSize(), QSize(320, 180));
        QVERIFY(view.player()->duration() > 2500);
    }

    void videoRotationMetadata()
    {
        MediaView view(m_settings.get());
        view.resize(400, 400);
        view.show();
        QString err;
        QVERIFY2(loadAndWait(view, u"portrait.mp4"_s, &err), qPrintable(err));
        QVERIFY(waitFor([&] {
            return view.surface()->frameSize().isValid();
        }));
        // Shown upright: the frame is taller than wide.
        QCOMPARE(view.surface()->frameSize(), QSize(180, 320));
    }

    void pauseAndSeekWithControls()
    {
        MediaView view(m_settings.get());
        view.resize(640, 360);
        view.show();
        QVERIFY(loadAndWait(view, u"clip.mkv"_s));
        QVERIFY(waitFor([&] {
            return view.player()->playbackState() == QMediaPlayer::PlayingState;
        }));
        view.controls()->playButton()->click();
        QCOMPARE(view.player()->playbackState(), QMediaPlayer::PausedState);
        // Seek by clicking at 2/3 of the slider.
        MediaSlider *slider = view.controls()->seekSlider();
        QTest::mouseClick(slider, Qt::LeftButton, {}, QPoint(slider->width() * 2 / 3, slider->height() / 2));
        QVERIFY(waitFor([&] {
            return view.player()->position() > 1500;
        }));
        view.controls()->playButton()->click();
        QCOMPARE(view.player()->playbackState(), QMediaPlayer::PlayingState);
    }

    void audioMiniPlayer()
    {
        MediaView view(m_settings.get());
        view.resize(400, 400);
        view.show();
        QString err;
        QVERIFY2(loadAndWait(view, u"tagged.flac"_s, &err), qPrintable(err));
        QVERIFY(!view.hasVideo());
        QCOMPARE(view.sizeCategory(), SizeCategory::Audio);
        QCOMPARE(view.title(), u"Cover Song"_s);
        QCOMPARE(view.artist(), u"Test Artist"_s);
        QVERIFY(waitFor([&] {
            return view.hasCoverArt();
        }));
        QVERIFY(waitFor([&] {
            return view.player()->position() > 200; // really playing
        }));
    }

    void audioWithoutCoverUsesPlaceholderAndFileName()
    {
        MediaView view(m_settings.get());
        view.show();
        QVERIFY(loadAndWait(view, u"plain.wav"_s));
        QVERIFY(!view.hasCoverArt());
        QCOMPARE(view.title(), u"plain.wav"_s);
        QCOMPARE(view.naturalSize(), MediaView::AudioWindowSize);
    }

    void audioOnlyContainerGetsAudioLayout()
    {
        MediaView view(m_settings.get());
        view.show();
        QSignalSpy category(&view, &ContentView::sizeCategoryChanged);
        QVERIFY(loadAndWait(view, u"audioonly.mkv"_s));
        QVERIFY(!view.hasVideo());
        QCOMPARE(view.sizeCategory(), SizeCategory::Audio);
        QVERIFY(!category.isEmpty());
    }

    void volumeWheelAndPersistence()
    {
        m_settings->setVolume(0.5);
        m_settings->setModuleVolume(SizeCategory::Audio,0.5);
        MediaView view(m_settings.get());
        view.resize(400, 400);
        view.show();
        QVERIFY(loadAndWait(view, u"plain.wav"_s));
        MediaSlider *vol = view.controls()->volumeSlider();
        QVERIFY(vol->isVisible());
        const QPointF pos(vol->width() / 2.0, vol->height() / 2.0);
        QWheelEvent up(pos, vol->mapToGlobal(pos), QPoint(), QPoint(0, 240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(vol, &up);
        QVERIFY(m_settings->moduleVolume(SizeCategory::Audio) > 0.55 && m_settings->moduleVolume(SizeCategory::Audio) < 0.65);
    }

    void switchingStopsPreviousStream()
    {
        MediaView view(m_settings.get());
        view.show();
        QVERIFY(loadAndWait(view, u"tagged.flac"_s));
        QVERIFY(loadAndWait(view, u"clip.mkv"_s));
        QCOMPARE(view.player()->source(), QUrl::fromLocalFile(path(u"clip.mkv"_s)));
        view.unload();
        QVERIFY(view.player()->source().isEmpty()); // decoder released
        QCOMPARE(view.player()->playbackState(), QMediaPlayer::StoppedState);
    }

    void brokenFileReportsError()
    {
        MediaView view(m_settings.get());
        view.show();
        QString err;
        QVERIFY(!loadAndWait(view, u"broken.mp4"_s, &err));
        QVERIFY(!err.isEmpty());
    }

    void repeatButtonControlsPlayback()
    {
        Settings settings(path(u"repeat-settings.json"_s));
        MediaView view(&settings);view.show();
        QVERIFY(loadAndWait(view,u"portrait.mp4"_s));
        auto* repeat=view.findChild<QAbstractButton*>(u"repeatPlayback"_s);
        QVERIFY(repeat);QVERIFY(repeat->isCheckable());QVERIFY(!repeat->isChecked());
        repeat->click();QVERIFY(repeat->isChecked());QVERIFY(settings.moduleLoopVideo());
        QTest::qWait(2200);
        QCOMPARE(view.player()->playbackState(),QMediaPlayer::PlayingState);
        repeat->click();QVERIFY(!repeat->isChecked());QVERIFY(!settings.moduleLoopVideo());
        QVERIFY(waitFor([&]{return view.player()->mediaStatus()==QMediaPlayer::EndOfMedia;},3000));
        QCOMPARE(view.player()->playbackState(),QMediaPlayer::StoppedState);
    }

    void loopVideo()
    {
        m_settings->setLoopVideo(true);
        MediaView view(m_settings.get());
        view.show();
        QVERIFY(loadAndWait(view, u"portrait.mp4"_s)); // 1 s long
        QTest::qWait(2500);
        QCOMPARE(view.player()->playbackState(), QMediaPlayer::PlayingState);
        m_settings->setLoopVideo(false);
    }
};

QTEST_MAIN(MediaViewTest)
#include "mediaviewtest.moc"
