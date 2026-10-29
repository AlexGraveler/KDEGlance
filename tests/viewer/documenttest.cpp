// SPDX-License-Identifier: GPL-3.0-or-later
#include "../testutils.h"
#include "previewwindow.h"
#include "viewerapp.h"
#include "views/officeview.h"
#include "views/pdfview.h"
#include "views/textview.h"
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCodec>
#include <QThreadPool>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTextBrowser>
using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;
class DocumentTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    void write(const QString& name, const QByteArray& data)
    {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), data.size());
    }
    PreviewItem item(const QString& name) { return ViewerApp::makeItem(QUrl::fromLocalFile(dir.filePath(name))); }
private Q_SLOTS:
    void initTestCase()
    {
        write(u"unicode.txt"_s, QString(u"Русский текст — Größe — English"_s).toUtf8());
        write(u"russian.log"_s, QTextCodec::codecForName("Windows-1251")->fromUnicode(u"Русский текст Русский текст"_s));
        write(u"german.txt"_s, QTextCodec::codecForName("Windows-1252")->fromUnicode(u"Größe und Straße"_s));
        write(u"utf16.txt"_s, QTextCodec::codecForName("UTF-16")->fromUnicode(u"Русский Größe English"_s));
        for (const auto* name : {"KOI8-R", "IBM 866"})
            write(QString::fromLatin1(name)+u".txt"_s, QTextCodec::codecForName(name)->fromUnicode(u"Русский текст Проверка кодировки и просмотра документа"_s));
        auto utf16le=QTextCodec::codecForName("UTF-16LE")->fromUnicode(u"Русский Größe English"_s);
        if (utf16le.startsWith("\xff\xfe")) utf16le.remove(0,2);
        write(u"utf16le-nobom.txt"_s,utf16le);
        write(u"sample.md"_s, "# Heading\n\n**bold**\n");
        write(u"broken.pdf"_s, "%PDF-1.7\nnot a PDF");
        QPdfWriter writer(dir.filePath(u"pages.pdf"_s));
        QPainter p(&writer);
        p.drawText(100, 300, u"First page text"_s);
        writer.newPage();
        p.drawText(100, 300, u"Second page text"_s);
        p.end();
    }
    void encodings_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        QTest::newRow("utf8") << u"unicode.txt"_s << u"Русский текст — Größe — English"_s;
        QTest::newRow("cp1251") << u"russian.log"_s << u"Русский текст Русский текст"_s;
        QTest::newRow("cp1252") << u"german.txt"_s << u"Größe und Straße"_s;
        QTest::newRow("koi8") << u"KOI8-R.txt"_s << u"Русский текст Проверка кодировки и просмотра документа"_s;
        QTest::newRow("cp866") << u"IBM 866.txt"_s << u"Русский текст Проверка кодировки и просмотра документа"_s;
        QTest::newRow("utf16le-nobom") << u"utf16le-nobom.txt"_s << u"Русский Größe English"_s;
        QTest::newRow("utf16") << u"utf16.txt"_s << u"Русский Größe English"_s;
    }
    void encodings()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);
        TextView v;
        v.show();
        v.load(item(name));
        QVERIFY(waitFor([&] { return v.editor()->toPlainText() == expected; }));
    }
    void markdownSourceAndRender()
    {
        TextView v;
        v.show();
        v.load(item(u"sample.md"_s));
        QVERIFY(waitFor([&] { return v.editor()->toPlainText().contains(u"Heading"_s); }));
        QKeyEvent toggle(QEvent::KeyPress,Qt::Key_M,Qt::NoModifier);
        QVERIFY(v.handleKey(&toggle));
        QVERIFY(v.renderedMarkdown());
    }
    void textCopiesWithoutContextMenu()
    {
        TextView view; view.show(); view.load(item(u"unicode.txt"_s));
        QVERIFY(waitFor([&]{return view.editor()->toPlainText().contains(u"Größe"_s);}));
        QKeyEvent select(QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier);
        QVERIFY(view.handleKey(&select));
        QCOMPARE(view.editor()->textCursor().selectedText(),view.editor()->toPlainText());
        QKeyEvent copy(QEvent::KeyPress,Qt::Key_C,Qt::ControlModifier);
        QVERIFY(view.handleKey(&copy));
        QCOMPARE(QApplication::clipboard()->text(),view.editor()->toPlainText());
    }
    void documentInterfaceHasNoPanels()
    {
        TextView text; PdfView pdf;
        text.show(); pdf.show();
        for (auto* view : {static_cast<ContentView*>(&text),static_cast<ContentView*>(&pdf)}) {
            QVERIFY(view->findChildren<QComboBox*>().isEmpty());
            QVERIFY(view->findChildren<QPushButton*>().isEmpty());
            QVERIFY(view->findChildren<QCheckBox*>().isEmpty());
            for (auto* area : view->findChildren<QAbstractScrollArea*>()) QCOMPARE(area->frameShape(),QFrame::NoFrame);
        }
        Settings settings(dir.filePath(u"minimal.json"_s)); PreviewWindow window(&settings);
        for (const auto& name : {u"unicode.txt"_s,u"pages.pdf"_s}) {
            window.showItem(item(name),true);
            QVERIFY(waitFor([&]{return window.isVisible();}));
            QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
            window.closePreview();
        }
    }
    void documentDragUsesSharedMove()
    {
        TextView text; PdfView pdf; text.show(); pdf.show();
        for(auto* view : {static_cast<ContentView*>(&text),static_cast<ContentView*>(&pdf)}) {
            auto* area=view->findChild<QAbstractScrollArea*>(); QVERIFY(area);
            QSignalSpy move(view,&ContentView::systemMoveRequested);
            for (auto mods : {Qt::NoModifier,Qt::AltModifier}) {
                auto* target=area->viewport();
                QTest::mousePress(target,Qt::LeftButton,mods,QPoint(50,50));
                QMouseEvent motion(QEvent::MouseMove,QPointF(100,100),target->mapToGlobal(QPoint(100,100)),Qt::NoButton,Qt::LeftButton,mods);
                QApplication::sendEvent(target,&motion);
                QTest::mouseRelease(target,Qt::LeftButton,mods,QPoint(100,100));
            }
            QCOMPARE(move.size(),2);
            QTest::mouseClick(area->viewport(),Qt::LeftButton,Qt::ShiftModifier,QPoint(50,50));
            QCOMPARE(move.size(),2);
        }
    }
    void textWheelIsExactlyDouble_data()
    {
        QTest::addColumn<int>("angle"); QTest::addColumn<int>("pixels"); QTest::addColumn<int>("count");
        QTest::newRow("wheel-down") << -120 << 0 << 1;
        QTest::newRow("wheel-up") << 120 << 0 << 1;
        QTest::newRow("high-resolution-angle") << -30 << 0 << 8;
        QTest::newRow("touchpad-pixels") << 0 << -7 << 40;
        QTest::newRow("touchpad-small") << 0 << 1 << 280;
    }
    void textWheelIsExactlyDouble()
    {
        QFETCH(int,angle); QFETCH(int,pixels); QFETCH(int,count);
        QByteArray lines; for(int i=0;i<2000;++i) lines += "Line of text for scrolling\n";
        write(u"scroll.txt"_s,lines);
        TextView view; view.resize(640,480); view.show(); view.load(item(u"scroll.txt"_s));
        QVERIFY(waitFor([&]{return view.editor()->toPlainText().count(QLatin1Char('\n'))==2000;}));
        QPlainTextEdit baseline; baseline.setReadOnly(true); baseline.setFrameShape(QFrame::NoFrame);
        baseline.setLineWrapMode(QPlainTextEdit::WidgetWidth); baseline.resize(view.editor()->size());
        baseline.setPlainText(QString::fromLatin1(lines)); baseline.show(); QTest::qWait(20);
        auto* fast=view.editor()->verticalScrollBar(); auto* normal=baseline.verticalScrollBar();
        normal->setValue(500); fast->setValue(500);
        if (pixels) count=baseline.fontMetrics().lineSpacing()*4;
        int normalRemainder=0;
        for (int i=0;i<count;++i) {
            if (pixels) {
                // Reference 1x pixel displacement in the editor's line units.
                normalRemainder += pixels;
                const int step=normalRemainder/baseline.fontMetrics().lineSpacing(); normal->setValue(normal->value()-step); normalRemainder-=step*baseline.fontMetrics().lineSpacing();
            } else {
                QWheelEvent event(QPointF(100,100),baseline.viewport()->mapToGlobal(QPoint(100,100)),{},QPoint(0,angle),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
                QApplication::sendEvent(baseline.viewport(),&event);
            }
            auto* target=view.editor()->viewport();
            QWheelEvent event(QPointF(100,100),target->mapToGlobal(QPoint(100,100)),QPoint(0,pixels),QPoint(0,angle),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
            QApplication::sendEvent(target,&event);
        }
        QVERIFY(normal->value()!=500);
        QCOMPARE(fast->value()-500,2*(normal->value()-500));
    }
    void pdfWheelSpeed_data()
    {
        QTest::addColumn<bool>("reflow");
        QTest::addColumn<QPoint>("angle");
        QTest::addColumn<QPoint>("pixels");
        QTest::addColumn<int>("count");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::addColumn<int>("factor");
        for (bool reflow : { false, true }) {
            const QByteArray prefix = reflow ? "reflow-" : "pages-";
            QTest::newRow((prefix + "down").constData()) << reflow << QPoint(0,-120) << QPoint() << 1 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "up").constData()) << reflow << QPoint(0,120) << QPoint() << 1 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "multi-notch").constData()) << reflow << QPoint(0,-600) << QPoint() << 1 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "diagonal-vertical").constData()) << reflow << QPoint(-30,-120) << QPoint() << 1 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "fine-angle").constData()) << reflow << QPoint(0,-30) << QPoint() << 8 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "pixels").constData()) << reflow << QPoint() << QPoint(0,-7) << 0 << Qt::KeyboardModifiers() << 3;
            QTest::newRow((prefix + "horizontal").constData()) << reflow << QPoint(-120,0) << QPoint() << 1 << Qt::KeyboardModifiers() << 1;
            QTest::newRow((prefix + "diagonal-horizontal").constData()) << reflow << QPoint(-120,30) << QPoint() << 1 << Qt::KeyboardModifiers() << 1;
            QTest::newRow((prefix + "ctrl-zoom").constData()) << reflow << QPoint(0,120) << QPoint() << 1 << Qt::KeyboardModifiers(Qt::ControlModifier) << 1;
        }
    }
    void pdfWheelSpeed()
    {
        QFETCH(bool,reflow); QFETCH(QPoint,angle); QFETCH(QPoint,pixels);
        QFETCH(int,count); QFETCH(Qt::KeyboardModifiers,modifiers); QFETCH(int,factor);
        const auto file = dir.filePath(u"scroll.pdf"_s);
        {
            QPdfWriter writer(file); writer.setResolution(72);
            QPainter painter(&writer);
            for (int page=0;page<12;++page) {
                if (page) writer.newPage();
                for (int line=0;line<70;++line)
                    painter.drawText(20,20+line*10,u"PDF wheel scrolling regression sample"_s);
            }
        }
        PdfView fast;
        QWidget referenceContainer;
        OfficeView office;
        auto* baseline=office.pdfView(); // The embedded Office PDF must retain 1x.
        baseline->setParent(&referenceContainer);
        referenceContainer.resize(640,480); referenceContainer.show();
        fast.resize(640,480); baseline->resize(640,480);
        fast.show(); baseline->show();
        fast.load(item(u"scroll.pdf"_s)); baseline->load(item(u"scroll.pdf"_s));
        QVERIFY(waitFor([&]{return fast.pageCount()==12 && baseline->pageCount()==12;}));
        if (reflow) {
            QKeyEvent key(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier);
            fast.handleKey(&key); baseline->handleKey(&key);
            QVERIFY(waitFor([&]{return fast.isReflow() && baseline->isReflow();}));
        } else {
            fast.setZoom(2); baseline->setZoom(2); // Both scroll axes have range.
        }
        auto* fastArea=reflow ? static_cast<QAbstractScrollArea*>(fast.findChild<QPlainTextEdit*>())
                              : fast.findChild<QAbstractScrollArea*>(u"pdfCanvas"_s);
        auto* normalArea=reflow ? static_cast<QAbstractScrollArea*>(baseline->findChild<QPlainTextEdit*>())
                                : baseline->findChild<QAbstractScrollArea*>(u"pdfCanvas"_s);
        QVERIFY(fastArea); QVERIFY(normalArea);
        QTest::qWait(20);
        QCOMPARE(fastArea->viewport()->size(),normalArea->viewport()->size());
        auto* v=fastArea->verticalScrollBar(); auto* ref=normalArea->verticalScrollBar();
        QCOMPARE(v->maximum(),ref->maximum());
        const int start=ref->maximum()/2;
        QVERIFY(start>100);
        v->setValue(start); ref->setValue(start);
        auto* h=fastArea->horizontalScrollBar(); auto* href=normalArea->horizontalScrollBar();
        h->setValue(h->maximum()/2); href->setValue(href->maximum()/2);
        const int hstart=href->value();
        if (!count) count=normalArea->fontMetrics().lineSpacing()*4;
        for (int i=0;i<count;++i) {
            for (auto* area : {normalArea,fastArea}) {
                QWheelEvent wheel(QPointF(100,100),area->viewport()->mapToGlobal(QPoint(100,100)),
                    pixels,angle,Qt::NoButton,modifiers,Qt::ScrollUpdate,false);
                QApplication::sendEvent(area->viewport(),&wheel);
            }
        }
        QCOMPARE(fast.zoom(),baseline->zoom());
        QCOMPARE(h->value()-hstart,href->value()-hstart);
        if (!(modifiers & Qt::ControlModifier)) {
            QCOMPARE(v->value()-start,factor*(ref->value()-start));
            if (!angle.isNull() && factor==3) QVERIFY(ref->value()!=start);
        } else {
            QCOMPARE(v->value(),ref->value());
        }
    }
    void largeTextIsIncrementalAndCancelable()
    {
        write(u"large.log"_s, QByteArray(9 * 1024 * 1024, 'x'));
        TextView v;
        v.show();
        v.load(item(u"large.log"_s));
        QTest::qWait(20);
        v.load(item(u"unicode.txt"_s));
        QVERIFY(waitFor([&] { return v.editor()->toPlainText().contains(u"Größe"_s); }));
        QVERIFY(v.editor()->toPlainText().size() < 1000);
    }
    void pdfContinuousRendering()
    {
        PdfView v;
        v.resize(700, 600);
        v.show();
        QSignalSpy fail(&v, &ContentView::failed);
        v.load(item(u"pages.pdf"_s));
        QVERIFY(waitFor([&] { return v.pageCount() == 2 && v.cachedPages() > 0; }));
        QVERIFY(fail.isEmpty());
        const auto z = v.zoom();
        v.setZoom(z * 1.2);
        QVERIFY(v.zoom() > z);
        QVERIFY(waitFor([&] { return v.cachedPages() > 0; }));
        QKeyEvent reflow(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier);
        QVERIFY(v.handleKey(&reflow));
        QVERIFY(waitFor([&] { return v.isReflow(); }));
        v.unload();
        QCOMPARE(v.pageCount(), 0);
    }
    void largePdfRendersOnlyVisiblePages()
    {
        const auto path = dir.filePath(u"many-pages.pdf"_s);
        {
            QPdfWriter writer(path);
            QPainter painter(&writer);
            for (int i = 0; i < 200; ++i) {
                if (i)
                    writer.newPage();
                painter.drawText(100, 300, u"Page %1"_s.arg(i + 1));
            }
        }
        PdfView view;
        view.resize(700, 600);
        view.show();
        view.load(item(u"many-pages.pdf"_s));
        QVERIFY(waitFor([&] { return view.pageCount() == 200 && view.cachedPages() > 0; }, 10000));
        QVERIFY(view.cachedPages() <= 4);
        auto* canvas = view.findChild<QAbstractScrollArea*>(u"pdfCanvas"_s);
        QVERIFY(canvas);
        canvas->verticalScrollBar()->setValue(canvas->verticalScrollBar()->maximum());
        QVERIFY(waitFor([&] { return view.cachedPages() > 1; }));
        QVERIFY(view.cachedPages() <= 4);
    }

    void pdfCorruptReportsError()
    {
        PdfView v;
        QSignalSpy failed(&v, &ContentView::failed);
        v.load(item(u"broken.pdf"_s));
        QVERIFY(waitFor([&] { return !failed.isEmpty(); }));
    }
    void pdfPasswordReportsError()
    {
        const QString output = dir.filePath(u"locked.pdf"_s);
        QVERIFY(
            run(u"/usr/bin/qpdf"_s, { u"--encrypt"_s, u"secret"_s, u"owner"_s, u"256"_s, u"--"_s, dir.filePath(u"pages.pdf"_s), output }));
        PdfView v;
        QSignalSpy failed(&v, &ContentView::failed);
        v.load(item(u"locked.pdf"_s));
        QVERIFY(waitFor([&] { return !failed.isEmpty(); }));
        QVERIFY(failed.first().first().toString().contains(u"password"_s));
    }
    void documentSizePersists()
    {
        Settings settings(dir.filePath(u"settings.json"_s));
        PreviewWindow w(&settings);
        w.setWorkArea(QRect(0, 0, 1600, 1000));
        w.showItem(item(u"unicode.txt"_s), true);
        QVERIFY(waitFor([&] { return w.isVisible(); }));
        w.beginInteractiveResize(Qt::RightEdge | Qt::BottomEdge, false);
        w.resize(620, 710);
        w.endInteractiveResize();
        w.showItem(item(u"pages.pdf"_s), false);
        QVERIFY(waitFor([&] { return w.isVisible(); }));
        QCOMPARE(w.size(), QSize(800, 700)); // PDF independent from the text geometry
        w.showItem(item(u"unicode.txt"_s),false);
        QVERIFY(waitFor([&]{return w.isVisible();}));
        QCOMPARE(w.size(),QSize(620,710));
    }
    void middleResetSeparatesPdfAndTextPresentation()
    {
        Settings settings(dir.filePath(u"reset-modules.json"_s));
        settings.setSavedSize(SizeCategory::Pdf,QSize(900,900)); settings.setSavedSize(SizeCategory::Text,QSize(620,710));
        settings.setSavedSize(SizeCategory::Image,QSize(555,333)); settings.setSyncSelection(false);
        PreviewWindow w(&settings); w.setWorkArea(QRect(0,0,1600,1000));
        w.showItem(item(u"pages.pdf"_s),true); QVERIFY(waitFor([&]{return w.pdfView()->cachedPages()>0;}));
        w.pdfView()->setZoom(2);
        QKeyEvent reflow(QEvent::KeyPress,Qt::Key_R,Qt::NoModifier); w.pdfView()->handleKey(&reflow);
        QVERIFY(waitFor([&]{return w.pdfView()->isReflow();}));
        auto reset=[&] {
            auto* target=w.currentView(); const auto pos=target->rect().center();
            QTest::mouseClick(target,Qt::MiddleButton,{},pos); QTest::mouseClick(target,Qt::MiddleButton,{},pos);
        };
        reset(); QVERIFY(!settings.savedSize(SizeCategory::Pdf)); QVERIFY(!w.pdfView()->isReflow());
        QCOMPARE(w.size(),QSize(800,700)); QCOMPARE(settings.savedSize(SizeCategory::Text),std::optional<QSize>(QSize(620,710)));
        w.showItem(item(u"sample.md"_s),true); QVERIFY(waitFor([&]{return w.textView()->editor()->toPlainText().contains(u"Heading"_s);}));
        w.textView()->editor()->zoomIn(3);
        w.textView()->toggleMarkdown(); QVERIFY(w.textView()->renderedMarkdown()); reset();
        QCOMPARE(w.textView()->editor()->document()->defaultFont(),QApplication::font("QPlainTextEdit"));
        QVERIFY(!w.textView()->renderedMarkdown()); QVERIFY(!settings.savedSize(SizeCategory::Text));
        QCOMPARE(settings.savedSize(SizeCategory::Image),std::optional<QSize>(QSize(555,333))); QVERIFY(!settings.syncSelection());
        QVERIFY(settings.save()); Settings reload(settings.filePath()); reload.load();
        QVERIFY(!reload.savedSize(SizeCategory::Pdf)); QVERIFY(!reload.savedSize(SizeCategory::Text));
        QCOMPARE(reload.savedSize(SizeCategory::Image),std::optional<QSize>(QSize(555,333)));
    }
    void repeatedPdfLoadsReleaseFileDescriptors()
    {
        PdfView view;
        view.resize(700, 600);
        view.show();
        view.load(item(u"pages.pdf"_s));
        QVERIFY(waitFor([&] { return view.cachedPages() > 0; }));
        view.unload();
        QThreadPool::globalInstance()->waitForDone();
        const auto count = [] { return QDir(u"/proc/self/fd"_s).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(); };
        const auto before = count();
        for (int i = 0; i < 30; ++i) {
            view.load(item(u"pages.pdf"_s));
            QVERIFY(waitFor([&] { return view.cachedPages() > 0; }));
            view.unload();
        }
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::sendPostedEvents();
        const auto after = count();
        qInfo() << "PDF descriptors before/after 30 loads:" << before << after;
        QVERIFY(after <= before + 4);
    }

    void officeCancelStopsProcess()
    {
        OfficeView v;
        QSignalSpy failed(&v, &ContentView::failed);
        v.load(item(u"unicode.txt"_s));
        QVERIFY(v.converting());
        v.unload();
        QVERIFY(!v.converting());
        QTest::qWait(300);
        QVERIFY(failed.isEmpty());
    }
};
QTEST_MAIN(DocumentTest)
#include "documenttest.moc"
