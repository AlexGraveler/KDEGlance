#include "l10n.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdfview.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCache>
#include <QKeyEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSet>
#include <QThreadPool>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <atomic>
#include <cmath>
#include <poppler-qt6.h>

namespace KDEGlance {
struct PdfSession {
    std::unique_ptr<Poppler::Document> document;
    QList<QSizeF> sizes;
    QMutex mutex;
    std::atomic_bool cancelled { false };
    std::atomic<quint64> revision { 0 };
};
struct PdfRenderCancellation {
    const PdfSession* session;
    quint64 revision;
};
static bool abortPdfRender(const QVariant& payload)
{
    const auto* context = reinterpret_cast<const PdfRenderCancellation*>(payload.value<quintptr>());
    return context->session->cancelled || context->session->revision != context->revision;
}
class PdfCanvas : public QAbstractScrollArea {
public:
    explicit PdfCanvas(QWidget* parent)
        : QAbstractScrollArea(parent)
        , cache(32768)
    {
        setObjectName(QStringLiteral("pdfCanvas"));
        setFocusPolicy(Qt::NoFocus);
        connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    }
    std::shared_ptr<PdfSession> session;
    QCache<int, QImage> cache;
    QSet<int> pending;
    QSet<int> failedPages;
    QList<QRectF> pages;
    qreal scale = 1;
    bool fit = true;
    void recalc()
    {
        if (!session)
            return;
        qreal widest = 1;
        for (auto size : session->sizes)
            widest = std::max(widest, size.width());
        if (fit)
            scale = std::clamp((viewport()->width() - 32) / widest, 0.05, 4.0);
        ++session->revision;
        pending.clear();
        failedPages.clear();
        cache.clear();
        pages.clear();
        qreal y = 16, width = std::max<qreal>(viewport()->width(), widest * scale + 32);
        for (auto size : session->sizes) {
            const auto s = size * scale;
            pages.append(QRectF((width - s.width()) / 2, y, s.width(), s.height()));
            y += s.height() + 16;
        }
        verticalScrollBar()->setPageStep(viewport()->height());
        verticalScrollBar()->setRange(0, int(std::min<qreal>(y, INT_MAX / 2)) - viewport()->height());
        horizontalScrollBar()->setPageStep(viewport()->width());
        horizontalScrollBar()->setRange(0, int(width) - viewport()->width());
        viewport()->update();
    }
    void zoomTo(qreal value)
    {
        const qreal fraction = qreal(verticalScrollBar()->value()) / std::max(1, verticalScrollBar()->maximum());
        fit = false;
        scale = std::clamp(value, 0.05, 4.0);
        recalc();
        verticalScrollBar()->setValue(qRound(fraction * verticalScrollBar()->maximum()));
    }

protected:
    void resizeEvent(QResizeEvent* e) override
    {
        QAbstractScrollArea::resizeEvent(e);
        if (fit)
            recalc();
    }
    void wheelEvent(QWheelEvent* e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            zoomTo(scale * std::pow(1.2, (e->angleDelta().y() ? e->angleDelta().y() / 120.0 : e->pixelDelta().y() / 60.0)));
            e->accept();
        } else
            QAbstractScrollArea::wheelEvent(e);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), QColor(75, 75, 75));
        if (!session)
            return;
        const QPointF scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
        const QRectF visible(scroll, QSizeF(viewport()->size()));
        for (int i = 0; i < pages.size(); ++i) {
            if (!pages[i].intersects(visible.adjusted(0, -80, 0, 80)))
                continue;
            const QRectF target = pages[i].translated(-scroll);
            painter.fillRect(target, Qt::white);
            if (failedPages.contains(i)) {
                painter.setPen(Qt::red);
                painter.drawText(target, Qt::AlignCenter, QStringLiteral("Cannot render this page"));
            } else if (const auto* image = cache.object(i))
                painter.drawImage(target, *image);
            else {
                painter.setPen(Qt::darkGray);
                painter.drawText(target, Qt::AlignCenter, QStringLiteral("%1…").arg(i + 1));
                requestPage(i);
            }
        }
    }

private:
    void requestPage(int page)
    {
        if (pending.contains(page) || !session)
            return;
        pending.insert(page);
        auto state = session;
        const auto revision = state->revision.load();
        const qreal resolution = 72 * scale * devicePixelRatioF();
        QPointer<PdfCanvas> guard(this);
        QThreadPool::globalInstance()->start([state, revision, page, resolution, guard] {
            QImage image;
            {
                QMutexLocker lock(&state->mutex);
                if (state->cancelled || state->revision != revision)
                    return;
                auto p = state->document->page(page);
                if (p) {
                    // Limit each raster to 16 MiB even for pathological page sizes.
                    const QSizeF size = p->pageSizeF();
                    const qreal factor = std::min(
                        1.0, std::sqrt((4.0 * 1024 * 1024) / (size.width() * size.height() * resolution * resolution / (72 * 72))));
                    const PdfRenderCancellation cancellation { state.get(), revision };
                    image = p->renderToImage(resolution * factor, resolution * factor, -1, -1, -1, -1, Poppler::Page::Rotate0, nullptr,
                        nullptr, abortPdfRender, QVariant::fromValue(reinterpret_cast<quintptr>(&cancellation)));
                }
            }
            QMetaObject::invokeMethod(
                qApp,
                [guard, state, revision, page, image] {
                    if (!guard || guard->session != state || state->revision != revision)
                        return;
                    guard->pending.remove(page);
                    if (!image.isNull())
                        guard->cache.insert(page, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
                    else
                        guard->failedPages.insert(page);
                    guard->viewport()->update();
                },
                Qt::QueuedConnection);
        });
    }
};
PdfView::PdfView(QWidget* parent, bool accelerateVerticalWheel)
    : ContentView(parent)
    , m_canvas(new PdfCanvas(this))
    , m_text(new QPlainTextEdit(this))
    , m_accelerateVerticalWheel(accelerateVerticalWheel)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_canvas);
    layout->addWidget(m_text);
    m_canvas->setFrameShape(QFrame::NoFrame);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setContextMenuPolicy(Qt::NoContextMenu);
    m_text->setReadOnly(true);
    m_text->setFocusPolicy(Qt::NoFocus);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->hide();
    enableDocumentDragging(m_canvas->viewport());
    enableDocumentDragging(m_text->viewport());
    m_canvas->verticalScrollBar()->installEventFilter(this);
    m_text->verticalScrollBar()->installEventFilter(this);
}
PdfView::~PdfView() { unload(); }
bool PdfView::eventFilter(QObject* watched, QEvent* event)
{
    if (m_accelerateVerticalWheel && !m_forwardingWheel && event->type() == QEvent::Wheel
        && (watched == m_canvas->viewport() || watched == m_text->viewport()
            || watched == m_canvas->verticalScrollBar() || watched == m_text->verticalScrollBar())) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const QPoint angle = wheel->angleDelta();
        const QPoint pixels = wheel->pixelDelta();
        const QPoint direction = angle.isNull() ? pixels : angle;
        // Keep Qt's original axis selection and leave Ctrl-wheel zoom untouched.
        if (!(wheel->modifiers() & Qt::ControlModifier) && direction.y() != 0
            && std::abs(direction.y()) >= std::abs(direction.x())) {
            QScopedValueRollback<bool> forwarding(m_forwardingWheel, true);
            bool accepted = false;
            // Repeat the original vertical step: Qt caps a multiplied wheel
            // delta to one page. Forward the horizontal component only once.
            for (int step = 0; step < 9; ++step) {
                QWheelEvent accelerated(wheel->position(), wheel->globalPosition(),
                    QPoint(step ? 0 : pixels.x(), pixels.y()),
                    QPoint(step ? 0 : angle.x(), angle.y()),
                    wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted(),
                    wheel->source(), wheel->pointingDevice());
                accelerated.setTimestamp(wheel->timestamp());
                QApplication::sendEvent(watched, &accelerated);
                accepted |= accelerated.isAccepted();
            }
            wheel->setAccepted(accepted);
            return true;
        }
    }
    return ContentView::eventFilter(watched, event);
}
void PdfView::resetPresentation()
{
    const auto font=QApplication::font("QPlainTextEdit");
    m_text->setFont(font); m_text->document()->setDefaultFont(font);
    setReflow(false); m_canvas->fit=true; m_canvas->recalc();
    m_canvas->verticalScrollBar()->setValue(0); m_canvas->horizontalScrollBar()->setValue(0);
}
void PdfView::unload()
{
    ++m_generation;
    if (m_session)
        m_session->cancelled = true;
    m_session.reset();
    m_canvas->session.reset();
    m_canvas->pages.clear();
    m_canvas->cache.clear();
    m_canvas->pending.clear();
    m_canvas->viewport()->update();
    m_text->clear();
    m_text->hide();
    m_canvas->show();
    setToolTip({});
    m_canvas->verticalScrollBar()->setValue(0);
    m_reflowRequested = false;
}
void PdfView::load(const PreviewItem& item)
{
    unload();
    const auto generation = m_generation;
    auto session = std::make_shared<PdfSession>();
    m_session = session;
    QPointer<PdfView> guard(this);
    setToolTip(tr_("Loading…"));
    QThreadPool::globalInstance()->start([session, generation, guard, path = item.path] {
        if (session->cancelled)
            return;
        QString error;
        session->document = Poppler::Document::load(path);
        if (!session->document)
            error = tr_("The PDF is damaged or cannot be read");
        else if (session->document->isLocked())
            error = tr_("This PDF is password protected; unlock it in a PDF application first");
        else if (session->document->numPages() > 20000)
            error = tr_("PDF exceeds the preview limit of 20,000 pages");
        else {
            session->document->setRenderHint(Poppler::Document::Antialiasing);
            session->document->setRenderHint(Poppler::Document::TextAntialiasing);
            for (int i = 0; i < session->document->numPages() && !session->cancelled; ++i) {
                auto page = session->document->page(i);
                const QSizeF size = page ? page->pageSizeF() : QSizeF();
                if (size.isEmpty()) {
                    error = tr_("The PDF contains an invalid page");
                    break;
                }
                session->sizes.append(size);
            }
            if (session->sizes.isEmpty() && error.isEmpty())
                error = tr_("The PDF has no pages");
        }
        if (session->cancelled)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [guard, session, generation, error] {
                if (!guard || guard->m_generation != generation)
                    return;
                if (!error.isEmpty()) {
                    Q_EMIT guard->failed(error);
                    return;
                }
                guard->m_canvas->session = session;
                guard->m_canvas->fit = true;
                guard->m_canvas->recalc();
                guard->setToolTip(tr_("%1 pages").arg(session->sizes.size()));
                Q_EMIT guard->ready();
            },
            Qt::QueuedConnection);
    });
}
int PdfView::pageCount() const { return m_canvas->session ? m_canvas->session->sizes.size() : 0; }
int PdfView::cachedPages() const { return m_canvas->cache.size(); }
qreal PdfView::zoom() const { return m_canvas->scale; }
void PdfView::setZoom(qreal value) { m_canvas->zoomTo(value); }
bool PdfView::isReflow() const { return !m_text->isHidden(); }
bool PdfView::handleKey(QKeyEvent* e)
{
    if (isReflow() && e->matches(QKeySequence::Copy)) { m_text->copy(); return true; }
    if (isReflow() && e->matches(QKeySequence::SelectAll)) { m_text->selectAll(); return true; }
    if (e->key() == Qt::Key_R && e->modifiers()==Qt::NoModifier) {
        setReflow(!m_reflowRequested); return true;
    }
    if ((e->key() == Qt::Key_0 || e->key() == Qt::Key_F) && e->modifiers()==Qt::NoModifier) {
        m_canvas->fit=true; m_canvas->recalc(); return true;
    }
    if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal) {
        setZoom(zoom() * 1.2);
        return true;
    }
    if (e->key() == Qt::Key_Minus) {
        setZoom(zoom() / 1.2);
        return true;
    }
    return false;
}
void PdfView::setReflow(bool enabled)
{
    m_reflowRequested = enabled;
    if (!enabled) {
        m_text->hide();
        m_canvas->show();
        return;
    }
    if (!m_canvas->session) {
        m_reflowRequested = false;
        return;
    }
    const auto state = m_session;
    const auto generation = m_generation;
    QPointer<PdfView> guard(this);
    setToolTip(tr_("Extracting text…"));
    QThreadPool::globalInstance()->start([state, generation, guard] {
        QString text;
        {
            QMutexLocker lock(&state->mutex);
            for (int i = 0; i < state->sizes.size() && !state->cancelled && text.size() < 2 * 1024 * 1024; ++i) {
                auto page = state->document->page(i);
                if (page)
                    text += page->text(QRectF()) + QLatin1Char('\n');
            }
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, state, generation, text] {
                if (!guard || guard->m_generation != generation || !guard->m_reflowRequested)
                    return;
                if (text.trimmed().isEmpty()) {
                    guard->setToolTip(tr_("No text layer; showing pages"));
                    guard->m_reflowRequested = false;
                    return;
                }
                guard->m_text->setPlainText(text);
                guard->m_canvas->hide();
                guard->m_text->show();
                guard->setToolTip(text.size() >= 2 * 1024 * 1024
                        ? tr_("Reflow: first 2 MiB of text (use pages for the full document)")
                        : tr_("Text reflow (layout simplified)"));
            },
            Qt::QueuedConnection);
    });
}
}
