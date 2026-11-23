#include "l10n.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "textview.h"
#include <QApplication>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>
#include <QKeyEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTextCodec>
#include <QTextCursor>
#include <QTextDocument>
#include <QThreadPool>
#include <QVBoxLayout>
#include <atomic>
#include <type_traits>

namespace KDEGlance {
struct TextSession {
    QString path;
    qint64 offset = 0;
    std::unique_ptr<QTextDecoder> decoder;
    std::atomic_bool cancelled { false };
};
// Keep native angular-wheel accumulation. Pixel-only touchpad events need an
// explicit path: QPlainTextEdit's native wheel handler ignores them on Qt 6.12.
template<class Base> class DoubleScroll : public Base {
public:
    using Base::Base;
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (event->pixelDelta().isNull() || (event->modifiers() & Qt::ControlModifier)) {
            Base::wheelEvent(event);
            if (!(event->modifiers() & Qt::ControlModifier)) Base::wheelEvent(event);
            return;
        }
        if (event->phase()==Qt::ScrollBegin) m_remainder={};
        const bool plain=std::is_same_v<Base,QPlainTextEdit>;
        m_remainder += event->pixelDelta() * 2;
        const int unit=plain ? this->fontMetrics().lineSpacing() : 1;
        const int lines=m_remainder.y()/unit;
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value()-lines);
        m_remainder.ry()-=lines*unit;
        this->horizontalScrollBar()->setValue(this->horizontalScrollBar()->value()-m_remainder.x());
        m_remainder.setX(0);
        event->accept();
    }
private:
    QPoint m_remainder;
};
TextView::TextView(QWidget* parent)
    : ContentView(parent)
    , m_repository(std::make_unique<KSyntaxHighlighting::Repository>())
    , m_editor(new DoubleScroll<QPlainTextEdit>(this))
    , m_markdown(new DoubleScroll<QTextBrowser>(this))
    , m_highlighter(new KSyntaxHighlighting::SyntaxHighlighter(m_editor->document()))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_editor);
    layout->addWidget(m_markdown);
    m_editor->setObjectName(QStringLiteral("textEditor"));
    m_markdown->setObjectName(QStringLiteral("markdownView"));
    m_editor->setFrameShape(QFrame::NoFrame);
    m_markdown->setFrameShape(QFrame::NoFrame);
    m_editor->setContextMenuPolicy(Qt::NoContextMenu);
    m_markdown->setContextMenuPolicy(Qt::NoContextMenu);
    m_editor->setReadOnly(true);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_editor->setFocusPolicy(Qt::NoFocus);
    m_markdown->setFocusPolicy(Qt::NoFocus);
    m_markdown->setOpenLinks(false);
    m_markdown->setOpenExternalLinks(false);
    m_markdown->hide();
    enableDocumentDragging(m_editor->viewport());
    enableDocumentDragging(m_markdown->viewport());
    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        auto* bar = m_editor->verticalScrollBar();
        if (bar->maximum()>0 && value >= bar->maximum()-bar->pageStep()) loadMore();
    });
    m_appendTimer.setInterval(0);
    connect(&m_appendTimer, &QTimer::timeout, this, [this] {
        const QString part = m_pending.mid(m_appended, 32768);
        QTextCursor cursor(m_editor->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(part);
        m_appended += part.size();
        if (!m_ready) {
            m_ready = true;
            Q_EMIT ready();
        }
        if (m_appended >= m_pending.size()) {
            m_appendTimer.stop();
            m_pending.clear();
            m_reading = false;
            applyMarkdownMode();
        }
    });
}
TextView::~TextView() { unload(); delete m_highlighter; }
bool TextView::renderedMarkdown() const { return !m_markdown->isHidden(); }
void TextView::toggleMarkdown()
{
    const auto suffix=QFileInfo(m_item.path).suffix().toLower();
    if (suffix!=QStringLiteral("md") && suffix!=QStringLiteral("markdown")) return;
    const bool render=m_markdown->isHidden();
    if (render) m_markdown->setMarkdown(m_editor->toPlainText());
    m_markdown->setVisible(render); m_editor->setVisible(!render);
    if (m_settings) {
        m_settings->setMarkdownRendered(render);
        Q_EMIT presentationSettingsChanged();
    }
    updateHighlighting();
}
void TextView::setSettings(Settings* settings)
{
    m_settings = settings;
    applySettings();
}
void TextView::applyMarkdownMode()
{
    const auto suffix = QFileInfo(m_item.path).suffix().toLower();
    const bool render = (suffix == QStringLiteral("md") || suffix == QStringLiteral("markdown"))
        && m_settings && m_settings->markdownRendered();
    if (render) m_markdown->setMarkdown(m_editor->toPlainText());
    m_markdown->setVisible(render);
    m_editor->setVisible(!render);
    updateHighlighting();
}
void TextView::applySettings() { applyMarkdownMode(); }
void TextView::updateHighlighting()
{
    auto definition = m_repository->definitionForFileName(m_item.path);
    if (!definition.isValid() && m_item.type.mimeType.isValid())
        definition = m_repository->definitionForMimeType(m_item.type.mimeType.name());
    if ((m_settings && !m_settings->syntaxHighlighting()) || renderedMarkdown() || !definition.isValid()) {
        // Detaching also removes existing formats and avoids highlighter work
        // altogether when the user chooses plain text.
        m_highlighter->setDocument(nullptr);
        m_highlighter->setDefinition({});
        return;
    }
    const auto theme = m_repository->defaultTheme(palette().color(QPalette::Base).lightnessF() < 0.5
        ? KSyntaxHighlighting::Repository::DarkTheme : KSyntaxHighlighting::Repository::LightTheme);
    m_highlighter->setTheme(theme);
    m_highlighter->setDefinition(definition);
    m_highlighter->setDocument(m_editor->document());
}
void TextView::changeEvent(QEvent* event)
{
    ContentView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        updateHighlighting();
}
bool TextView::handleKey(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy)) {
        if (renderedMarkdown()) m_markdown->copy(); else m_editor->copy();
        return true;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        if (renderedMarkdown()) m_markdown->selectAll(); else m_editor->selectAll();
        return true;
    }
    if (event->key()==Qt::Key_M && event->modifiers()==Qt::NoModifier) { toggleMarkdown(); return true; }
    if (event->key()==Qt::Key_PageDown && event->modifiers()==Qt::ControlModifier) { loadMore(); return true; }
    return false;
}
void TextView::resetPresentation()
{
    const auto plainFont=QApplication::font("QPlainTextEdit");
    const auto markdownFont=QApplication::font("QTextBrowser");
    m_editor->setFont(plainFont); m_editor->document()->setDefaultFont(plainFont);
    m_markdown->setFont(markdownFont); m_markdown->document()->setDefaultFont(markdownFont);
    m_markdown->hide(); m_editor->show();
    m_editor->verticalScrollBar()->setValue(0);
    m_editor->horizontalScrollBar()->setValue(0);
    applySettings();
}
void TextView::unload()
{
    ++m_generation;
    if (m_session)
        m_session->cancelled = true;
    m_session.reset();
    m_appendTimer.stop();
    m_pending.clear();
    m_reading = false;
    m_ready = false;
    m_item = { };
    m_editor->clear();
    m_markdown->clear();
    m_markdown->hide();
    m_editor->show();
    m_hasMore = false;
}
void TextView::load(const PreviewItem& item)
{
    unload();
    m_item = item;
    m_session = std::make_shared<TextSession>();
    m_session->path = item.path;
    applySettings();
    beginRead();
}
void TextView::loadMore()
{
    if (!m_reading && m_hasMore && m_session)
        beginRead();
}
void TextView::beginRead()
{
    m_reading = true;
    m_hasMore = false;
    const auto session = m_session;
    const auto generation = m_generation;
    QPointer<TextView> guard(this);
    QThreadPool::globalInstance()->start([session, generation, guard] {
        if (session->cancelled)
            return;
        QFile file(session->path);
        QString error, text;
        bool more = false;
        if (!file.open(QIODevice::ReadOnly) || !file.seek(session->offset))
            error = tr_("Cannot read text: %1").arg(file.errorString());
        else {
            const QByteArray data = file.read(8 * 1024 * 1024);
            if (file.error() != QFileDevice::NoError)
                error = file.errorString();
            if (!session->decoder) {
                QTextCodec* codec = QTextCodec::codecForUtfText(data, nullptr);
                if (!codec) {
                    // BOM-less UTF-16: ASCII code units have a predictable NUL lane.
                    qsizetype even=0, odd=0;
                    for (qsizetype i=0;i<data.size();++i) if (!data[i]) (i%2 ? odd : even)++;
                    if (odd > data.size()/8 && even < odd/4) codec=QTextCodec::codecForName("UTF-16LE");
                    else if (even > data.size()/8 && odd < even/4) codec=QTextCodec::codecForName("UTF-16BE");
                    else {
                        QTextCodec::ConverterState state;
                        QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
                        if (!state.invalidChars) codec=QTextCodec::codecForName("UTF-8");
                        else {
                            // Ambiguous legacy encodings: score Cyrillic word structure,
                            // favour Western text unless a Cyrillic candidate is convincing.
                            const auto sample=data.left(65536);
                            double best=0;
                            codec=QTextCodec::codecForName("Windows-1252");
                            const QString pairs=QStringLiteral(" ст но то на ен ов ни ра во ко пр ро по ос не го ал ли ре от та ер ор ка те ет ан ит де ла ел ил ол тр ве ти ес ва");
                            for (const auto* name : {"Windows-1251", "KOI8-R", "IBM 866"}) {
                                auto* candidate=QTextCodec::codecForName(name);
                                if (!candidate) continue;
                                const QString text=candidate->toUnicode(sample).toLower();
                                double score=0; int letters=0;
                                for (qsizetype i=0;i<text.size();++i) {
                                    const ushort c=text[i].unicode();
                                    if (c>=0x430 && c<=0x44f) { ++letters; score+=0.1; }
                                    else if ((c>=0x2500 && c<=0x259f) || (c>=0x400 && c<=0x42f)) score-=2;
                                    if (i && pairs.contains(QStringLiteral(" ")+text.mid(i-1,2))) score+=2;
                                }
                                if (letters>=3 && score>best && score>letters*0.35) { best=score; codec=candidate; }
                            }
                        }
                    }
                }
                if (!codec)
                    error = tr_("Unsupported text encoding");
                else
                    session->decoder.reset(codec->makeDecoder());
            }
            if (session->decoder)
                text = session->decoder->toUnicode(data);
            session->offset = file.pos();
            more = !file.atEnd();
        }
        if (session->cancelled)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [guard, session, generation, error, text, more] {
                if (!guard || guard->m_generation != generation)
                    return;
                if (!error.isEmpty()) {
                    guard->m_reading = false;
                    Q_EMIT guard->failed(error);
                    return;
                }
                guard->m_pending = text;
                guard->m_appended = 0;
                guard->m_hasMore = more;
                guard->m_appendTimer.start();
            },
            Qt::QueuedConnection);
    });
}
}
