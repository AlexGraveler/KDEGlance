// SPDX-License-Identifier: GPL-3.0-or-later
#include "../testutils.h"
#include "previewwindow.h"
#include "settingsdialog.h"
#include "style.h"
#include "viewerapp.h"
#include "views/mediacontrols.h"
#include "views/mediaview.h"
#include "views/textview.h"
#include "views/infoview.h"
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QCursor>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSyntaxHighlighter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextLayout>
using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;
class PointerTrace : public QObject {
public:
    QWidget* window; QElapsedTimer clock;
    explicit PointerTrace(QWidget* w): window(w) {clock.start(); if(qEnvironmentVariableIsSet("KDEGLANCE_TRACE_POINTER")) qApp->installEventFilter(this);}
    bool eventFilter(QObject* target,QEvent* event) override {
        auto* widget=qobject_cast<QWidget*>(target);
        if(widget && widget->window()==window && (event->type()==QEvent::Enter || event->type()==QEvent::Leave || event->type()==QEvent::MouseMove))
            qInfo()<<"Pointer"<<clock.elapsed()<<target->objectName()<<event->type()<<event->spontaneous();
        return false;
    }
};
class PresentationTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString path(const QString& name) { return dir.filePath(name); }
    PreviewItem item(const QString& name) { return ViewerApp::makeItem(QUrl::fromLocalFile(path(name))); }
    void write(const QString& name, const QByteArray& bytes)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
    void move(QWidget* widget)
    {
        QMouseEvent e(QEvent::MouseMove, QPointF(40, 40), QPointF(widget->mapToGlobal(QPoint(40, 40))), Qt::NoButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &e);
    }
private Q_SLOTS:
    void initTestCase()
    {
        write(u"one.md"_s, "# First\n\n**bold**\n");
        write(u"two.markdown"_s, "# Second\n");
        write(u"sample.cpp"_s, "// highlighted comment\nint main() { return 42; }\n");
        write(u"sample.py"_s, "# Python comment\nprint('hello')\n");
        write(u"plain.txt"_s, "Ordinary text\n");
        QImage image(640, 480, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(path(u"фото <пример>.png"_s)));
    }
    void persistentSettings()
    {
        Settings s(path(u"settings.json"_s));
        QVERIFY(!s.markdownRendered());
        QVERIFY(s.syntaxHighlighting());
        s.setMarkdownRendered(true);
        s.setSyntaxHighlighting(false);
        s.setFilenamePosition(Settings::FilenamePosition::Bottom);
        s.setSavedSize(SizeCategory::Image, QSize(500, 350));
        QVERIFY(s.save());
        Settings reloaded(s.filePath());
        reloaded.load();
        QVERIFY(reloaded.markdownRendered());
        QVERIFY(!reloaded.syntaxHighlighting());
        QCOMPARE(reloaded.filenamePosition(), Settings::FilenamePosition::Bottom);
        reloaded.resetModule(SizeCategory::Audio);
        QVERIFY(reloaded.markdownRendered());
        QVERIFY(!reloaded.syntaxHighlighting());
        reloaded.resetModule(SizeCategory::Text);
        QVERIFY(!reloaded.markdownRendered());
        QVERIFY(reloaded.syntaxHighlighting());
        QCOMPARE(reloaded.filenamePosition(), Settings::FilenamePosition::Bottom);
        QCOMPARE(*reloaded.savedSize(SizeCategory::Image), QSize(500, 350));
    }
    void markdownAcrossFilesAndRestart()
    {
        Settings s(path(u"markdown.json"_s));
        {
            PreviewWindow w(&s);
            w.showItem(item(u"one.md"_s), true);
            QTRY_VERIFY(w.textView()->editor()->toPlainText().contains(u"First"_s));
            QVERIFY(!w.textView()->renderedMarkdown());
            QTest::keyClick(&w, Qt::Key_M);
            QVERIFY(w.textView()->renderedMarkdown());
            QVERIFY(s.markdownRendered());
            w.closePreview();
            QVERIFY(s.save());
        }
        Settings reloaded(s.filePath());
        reloaded.load();
        PreviewWindow w(&reloaded);
        w.showItem(item(u"two.markdown"_s), true);
        QTRY_VERIFY(w.textView()->editor()->toPlainText().contains(u"Second"_s));
        QTRY_VERIFY(w.textView()->renderedMarkdown());
        w.showItem(item(u"plain.txt"_s), true);
        QTRY_VERIFY(w.textView()->editor()->toPlainText().contains(u"Ordinary"_s));
        QVERIFY(!w.textView()->renderedMarkdown());
        w.showItem(item(u"one.md"_s), true);
        QTRY_VERIFY(w.textView()->renderedMarkdown());
        QTest::keyClick(&w, Qt::Key_M);
        QVERIFY(!reloaded.markdownRendered());
        w.closePreview();
    }
    void syntaxAndTheme()
    {
        Settings s(path(u"syntax.json"_s));
        TextView view;
        view.setSettings(&s);
        view.show();
        view.load(item(u"sample.cpp"_s));
        QTRY_VERIFY(view.editor()->document()->firstBlock().layout()->formats().size() > 0);
        auto* highlighter
            = dynamic_cast<KSyntaxHighlighting::SyntaxHighlighter*>(view.editor()->document()->findChild<QSyntaxHighlighter*>());
        QVERIFY(highlighter);
        QVERIFY(highlighter->definition().isValid());
        QCOMPARE(highlighter->definition().name(), u"C++"_s);
        s.setSyntaxHighlighting(false);
        view.applySettings();
        QTRY_VERIFY(view.editor()->document()->firstBlock().layout()->formats().isEmpty());
        QVERIFY(!highlighter->definition().isValid());
        s.setSyntaxHighlighting(true);
        view.applySettings();
        QTRY_VERIFY(!view.editor()->document()->firstBlock().layout()->formats().isEmpty());
        QPalette palette = view.palette();
        palette.setColor(QPalette::Base, QColor(22, 22, 22));
        palette.setColor(QPalette::Text, Qt::white);
        view.setPalette(palette);
        QTRY_VERIFY(QColor(highlighter->theme().editorColor(KSyntaxHighlighting::Theme::BackgroundColor)).lightnessF() < 0.5);
        const auto dark = highlighter->theme().name();
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::Text, Qt::black);
        view.setPalette(palette);
        QTRY_VERIFY(highlighter->theme().name() != dark);
        view.load(item(u"sample.py"_s));
        QTRY_COMPARE(highlighter->definition().name(), u"Python"_s);
        QVERIFY(view.editor()->isReadOnly());
    }
    void nativeSettingsControls()
    {
        Settings s(path(u"dialog.json"_s));
        s.setModuleLoopVideo(true);
        SettingsDialog dialog(&s);
        dialog.show();
        for (auto *box : dialog.findChildren<QCheckBox*>()) {
            QVERIFY(box->text()!=QStringLiteral("Repeat videos"));
            QVERIFY(box->text()!=QStringLiteral("Повторять видео"));
        }
        auto* syntax = dialog.findChild<QCheckBox*>(u"syntaxHighlighting"_s);
        QVERIFY(syntax);
        QVERIFY(syntax->isChecked());
        syntax->click();
        QVERIFY(!s.syntaxHighlighting());
        QVERIFY(s.moduleLoopVideo()); // Editing unrelated settings must not reset repeat.
        auto* group = dialog.findChild<QButtonGroup*>();
        QVERIFY(group);
        QCOMPARE(group->buttons().size(), 3);
        QVERIFY(group->exclusive());
        group->button(2)->click();
        QCOMPARE(s.filenamePosition(), Settings::FilenamePosition::Bottom);
        QVERIFY(dialog.styleSheet().isEmpty());
        QCOMPARE(dialog.palette(), QApplication::palette(&dialog));
        QCOMPARE(dialog.font(), QApplication::font(&dialog));
        const auto out = qEnvironmentVariable("KDEGLANCE_REVIEW_DIR");
        if (!out.isEmpty())
            QVERIFY(dialog.grab().save(out + u"/settings.png"_s));
        PreviewWindow preview(&s);
        auto *repeat=preview.mediaView()->findChild<QAbstractButton*>(u"repeatPlayback"_s);
        QVERIFY(repeat);
        s.setModuleLoopAudio(true); preview.applySettings(); QVERIFY(repeat->isChecked());
        s.resetModule(SizeCategory::Audio); preview.applySettings(); QVERIFY(!repeat->isChecked());
        QVERIFY(s.moduleLoopVideo());
    }
    void filenameActivity()
    {
        Settings s(path(u"filename.json"_s));
        s.setCloseOnFocusLoss(false);
        PreviewWindow w(&s);
        w.showItem(item(u"фото <пример>.png"_s), true);
        QTRY_VERIFY(w.isVisible());
        PointerTrace trace(&w);
        // Drain compositor map/enter/leave events before synthetic pointer input.
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(1200);
        move(w.currentView());
        auto* overlay = w.findChild<QWidget*>(u"filenameOverlay"_s);
        QVERIFY(overlay);
        QTRY_VERIFY(overlay->isVisible());
        QCOMPARE(overlay->property("filename").toString(), u"фото <пример>.png"_s);
        QVERIFY(overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
        QVERIFY(overlay->height() > overlay->fontMetrics().height()+14);
        QImage caption(overlay->size(),QImage::Format_ARGB32_Premultiplied);
        caption.fill(Qt::transparent);
        overlay->render(&caption,QPoint(),QRegion(),QWidget::DrawChildren);
        QCOMPARE(qAlpha(caption.pixel(caption.width()/2,2)),0);
        s.setFilenamePosition(Settings::FilenamePosition::Bottom);
        w.applySettings();
        QVERIFY(overlay->y() > w.height() / 2);
        s.setFilenamePosition(Settings::FilenamePosition::Hidden);
        w.applySettings();
        QVERIFY(!overlay->isVisible());
        move(w.currentView());
        QVERIFY(!overlay->isVisible());
        s.setFilenamePosition(Settings::FilenamePosition::Top);
        w.applySettings();
        QVERIFY(overlay->isVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&w, &leave);
        QVERIFY(!overlay->isVisible());
        move(w.currentView());
        QVERIFY(overlay->isVisible());
        QElapsedTimer clock; clock.start();
        QTest::qWait(2800);
        if (!overlay->isVisible()) qWarning() << "Filename hidden after" << clock.elapsed() << "ms; cursor" << QCursor::pos() << "window" << w.geometry();
        QVERIFY(overlay->isVisible());
        move(w.currentView());
        QTest::qWait(300);
        QVERIFY(overlay->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!overlay->isVisible(), 3300);
        w.closePreview();
    }
    void videoControlsIdleAndInteraction()
    {
        if (ffmpeg().isEmpty())
            QSKIP("FFmpeg missing");
        QVERIFY(run(ffmpeg(),
            { u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s, u"color=c=blue:size=320x180:rate=10"_s, u"-t"_s,
                u"12"_s, u"-c:v"_s, u"mpeg4"_s, path(u"clip.mkv"_s) }));
        Settings s(path(u"video.json"_s));
        s.setMuted(true);
        s.setCloseOnFocusLoss(false);
        PreviewWindow w(&s);
        w.showItem(item(u"clip.mkv"_s), true);
        auto* media = w.mediaView();
        PointerTrace trace(&w);
        QTRY_VERIFY_WITH_TIMEOUT(media->surface()->hasFrame(), 15000);
        media->player()->pause();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QTest::qWait(1200);
        move(media->surface());
        QVERIFY(media->controls()->isVisible());
        QTest::qWait(2750);
        QVERIFY(media->controls()->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!media->controls()->isVisible(), 1000);
        auto* overlay = w.findChild<QWidget*>(u"filenameOverlay"_s);
        QVERIFY(!overlay->isVisible());
        move(media->surface());
        QVERIFY(media->controls()->isVisible());
        QVERIFY(overlay->isVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&w, &leave);
        QVERIFY(!media->controls()->isVisible());
        QVERIFY(!overlay->isVisible());
        move(media->surface());
        auto* volume = media->controls()->volumeSlider();
        QTest::mousePress(volume, Qt::LeftButton, Qt::NoModifier, volume->rect().center());
        QTest::qWait(3250);
        QVERIFY(media->controls()->isVisible());
        QApplication::sendEvent(&w, &leave);
        QVERIFY(media->controls()->isVisible());
        QTest::mouseRelease(volume, Qt::LeftButton, Qt::NoModifier, volume->rect().center());
        QTRY_VERIFY(!media->controls()->isVisible());
        w.closePreview();
        QCOMPARE(media->player()->playbackState(), QMediaPlayer::StoppedState);
    }
    void audioPlaceholderInWindow()
    {
        if (ffmpeg().isEmpty()) QSKIP("FFmpeg missing");
        QVERIFY(run(ffmpeg(), {u"-loglevel"_s,u"error"_s,u"-y"_s,u"-f"_s,u"lavfi"_s,u"-i"_s,
            u"sine=duration=2"_s,path(u"plain.wav"_s)}));
        Settings s(path(u"audio.json"_s)); s.setMuted(true); s.setAutoplay(false); s.setCloseOnFocusLoss(false);
        PreviewWindow w(&s); w.showItem(item(u"plain.wav"_s),true);
        QTRY_VERIFY(w.mediaView()->isLoaded()); QVERIFY(!w.mediaView()->hasCoverArt());
        QVERIFY(QTest::qWaitForWindowExposed(&w)); QTest::qWait(300);
        move(w.mediaView());
        QImage expected(w.size(),QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::transparent);
        QPainter painter(&expected);
        Style::paintAudioPlaceholder(painter,QRectF(QPointF(),QSizeF(w.size())),false);
        painter.end();
        const QPoint sample(w.width()/2,w.height()-3);
        QCOMPARE(w.grab().toImage().pixelColor(sample),expected.pixelColor(sample));
        const auto out=qEnvironmentVariable("KDEGLANCE_REVIEW_DIR");
        if(!out.isEmpty()) QVERIFY(w.grab().save(out+u"/audio-window.png"_s));
        auto* volume=w.mediaView()->controls()->volumeSlider();
        QTest::mousePress(volume,Qt::LeftButton,Qt::NoModifier,volume->rect().center());
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&w,&leave);
        QTest::mouseRelease(volume,Qt::LeftButton,Qt::NoModifier,volume->rect().center());
        QTest::qWait(50);
        QVERIFY(!w.mediaView()->controls()->isVisible());
        move(w.mediaView());
        QVERIFY(w.mediaView()->controls()->isVisible());
        w.closePreview();
    }
    void videoWheelResizesBeforeCropping()
    {
        if (ffmpeg().isEmpty()) QSKIP("FFmpeg missing");
        QVERIFY(run(ffmpeg(), {u"-loglevel"_s,u"error"_s,u"-y"_s,u"-f"_s,u"lavfi"_s,u"-i"_s,
            u"color=c=blue:size=320x180:rate=10"_s,u"-t"_s,u"3"_s,u"-c:v"_s,u"mpeg4"_s,path(u"zoom.mkv"_s)}));
        Settings s(path(u"zoom.json"_s)); s.setMuted(true); s.setCloseOnFocusLoss(false);
        s.setSavedSize(SizeCategory::Video,QSize(640,360));
        const auto settingsBefore=s.toJson();
        PreviewWindow w(&s); w.setWorkArea(QRect(0,0,960,640));
        w.showItem(item(u"zoom.mkv"_s),true);
        auto* media=w.mediaView(); auto* surface=media->surface();
        QTRY_VERIFY_WITH_TIMEOUT(surface->hasFrame(),15000); media->player()->pause();
        QVERIFY(QTest::qWaitForWindowExposed(&w)); QTest::qWait(1200);
        const auto wheel=[&](int delta, bool pixels=false) {
            const QPointF pos(surface->width()/2.0,surface->height()/3.0);
            QWheelEvent e(pos,surface->mapToGlobal(pos.toPoint()),pixels?QPoint(0,delta):QPoint(),
                pixels?QPoint():QPoint(0,delta),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(surface,&e);
            QCoreApplication::processEvents();
        };
        const auto fills=[&] {
            const auto rect=surface->imageRect();
            return std::abs(rect.width()-w.width())<2 && std::abs(rect.height()-w.height())<2;
        };
        const QSize initial=w.size(); QVERIFY(fills());
        wheel(-120); QVERIFY(w.width()<initial.width()); QVERIFY(w.height()<initial.height()); QVERIFY(fills()); QCOMPARE(surface->zoom(),1.0);
        const auto smaller=w.size(); wheel(120); QVERIFY(w.width()>smaller.width()); QVERIFY(fills());
        for(int i=0;i<7;++i) wheel(120);
        const auto capped=w.size(); QVERIFY(capped.width()<=960 && capped.height()<=640);
        QVERIFY(capped.width()==960 || capped.height()==640); QVERIFY(surface->zoom()>1.0);
        const auto magnified=surface->imageRect().size(); wheel(120);
        QCOMPARE(w.size(),capped); QVERIFY(surface->imageRect().width()>magnified.width());
        for(int i=0;i<10;++i) wheel(-120);
        QVERIFY(w.width()<capped.width()); QVERIFY(fills());
        const auto beforePixel=w.width(); wheel(60,true); QVERIFY(w.width()>beforePixel); QVERIFY(fills());
        QCOMPARE(s.toJson(),settingsBefore);
        // The temporary wheel size must not replace remembered manual geometry.
        w.closePreview(); w.showItem(item(u"zoom.mkv"_s),true);
        QTRY_VERIFY(surface->hasFrame()); QCOMPARE(w.size(),initial);
        surface->rotateLeft(); QTRY_COMPARE(surface->frameSize(),QSize(180,320));
        wheel(120); QVERIFY(fills()); QVERIFY(w.width()<=960 && w.height()<=640);
        QCOMPARE(s.toJson(),settingsBefore);
        w.beginInteractiveResize(Qt::BottomEdge|Qt::RightEdge,false);
        w.resize(800,600); QTest::qWait(120);
        wheel(-120);
        QCOMPARE(*s.savedSize(SizeCategory::Video),QSize(800,600));
        QVERIFY(w.size()!=QSize(800,600)); QVERIFY(fills());
        w.closePreview(); w.showItem(item(u"zoom.mkv"_s),true);
        QTRY_VERIFY(surface->hasFrame()); QCOMPARE(w.size(),QSize(800,450));
        QCOMPARE(*s.savedSize(SizeCategory::Video),QSize(800,600));
        const auto out=qEnvironmentVariable("KDEGLANCE_REVIEW_DIR");
        if(!out.isEmpty()) QVERIFY(w.grab().save(out+u"/video-window.png"_s));
        w.closePreview();
    }
    void controlsStartHiddenOutside()
    {
        Settings s(path(u"outside.json"_s)); s.setCloseOnFocusLoss(false); s.setAutoplay(false);
        // An unattached media view receives no pointer-enter events.
        MediaView media(&s);
        media.load(item(u"plain.txt"_s));
        QVERIFY(media.controls()->isHidden());
        media.notifyUserActivity(); QVERIFY(!media.controls()->isHidden());
        media.pointerLeftWindow(); QVERIFY(media.controls()->isHidden());
        media.resetPresentation(); QVERIFY(media.controls()->isHidden());
        media.unload();
        PreviewWindow w(&s);
        w.showItem(item(u"plain.txt"_s),true);
        QTRY_VERIFY(w.textView()->editor()->toPlainText().contains(u"Ordinary"_s));
        QVERIFY(QTest::qWaitForWindowExposed(&w)); QTest::qWait(1200);
        if (QGuiApplication::platformName()==u"offscreen"_s)
            QCursor::setPos(w.mapToGlobal(QPoint(-100,-100)));
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&w,&leave);
        auto* overlay=w.findChild<QWidget*>(u"filenameOverlay"_s); QVERIFY(overlay);
        QVERIFY(!overlay->isVisible());
        w.showItem(item(u"one.md"_s),true);
        QTRY_VERIFY(w.textView()->editor()->toPlainText().contains(u"First"_s));
        QVERIFY(!overlay->isVisible());
        QTest::keyClick(&w,Qt::Key_M); QVERIFY(!overlay->isVisible());
        move(w.currentView()); QVERIFY(overlay->isVisible());
        QApplication::sendEvent(&w,&leave); QVERIFY(!overlay->isVisible());
        w.closePreview();
    }
    void informationCardUsesSharedFilename()
    {
        write(u"unknown.bin"_s,QByteArray::fromHex("00819bff00852a00"));
        Settings s(path(u"info-name.json"_s)); s.setCloseOnFocusLoss(false);
        PreviewWindow w(&s); w.showItem(item(u"unknown.bin"_s),true);
        QTRY_COMPARE(w.currentView(),static_cast<ContentView*>(w.infoView()));
        QTRY_COMPARE(w.infoView()->headline(),u"unknown.bin"_s);
        QTRY_VERIFY(w.isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(&w)); QTest::qWait(1200);
        for (auto *label : w.infoView()->findChildren<QLabel*>())
            if (label->text()==u"unknown.bin"_s) QVERIFY(label->isHidden());
        move(w.currentView());
        auto *overlay=w.findChild<QWidget*>(u"filenameOverlay"_s); QVERIFY(overlay);
        QVERIFY(overlay->isVisible()); QCOMPARE(overlay->property("filename").toString(),u"unknown.bin"_s);
        QVERIFY(overlay->height() > overlay->fontMetrics().height()+14);
        w.closePreview();
    }
    void audioUsesOnlySharedFilename()
    {
        MediaControls controls; controls.setMode(MediaControls::Mode::Audio);
        controls.resize(600,120); controls.setScrimEnabled(false); controls.show();
        controls.setTrackInfo(u"first-file.flac"_s,u"Artist"_s);
        const auto first=controls.grab().toImage();
        controls.setTrackInfo(u"second-file.flac"_s,u"Artist"_s);
        QCOMPARE(controls.grab().toImage(),first);
        // Artist metadata is intentionally retained, unlike the duplicate title.
        controls.setTrackInfo(u"second-file.flac"_s,QString());
        QVERIFY(controls.grab().toImage()!=first);
    }
    void audioArtworkScales()
    {
        const auto out = qEnvironmentVariable("KDEGLANCE_REVIEW_DIR");
        for (const auto size : { QSize(440, 440), QSize(760, 280), QSize(280, 650) }) {
            QImage image(size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            Style::paintAudioPlaceholder(painter, QRectF(QPointF(), QSizeF(size)), false);
            painter.end();
            QCOMPARE(qAlpha(image.pixel(0, 0)), 255);
            QCOMPARE(qAlpha(image.pixel(size.width() - 1, size.height() - 1)), 255);
            if (!out.isEmpty())
                QVERIFY(image.save(out + u"/audio-%1x%2.png"_s.arg(size.width()).arg(size.height())));
        }
    }
};
QTEST_MAIN(PresentationTest)
#include "presentationtest.moc"
