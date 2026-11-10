// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "previewwindow.h"

#include "config-kdeglance.h"
#include "geometrymath.h"
#include "l10n.h"
#include "style.h"
#include "remoteresolver.h"
#include "views/imageview.h"
#include "views/infoview.h"
#include "views/mediaview.h"
#include "views/textview.h"
#include "views/pdfview.h"
#include "views/officeview.h"

#include <QApplication>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QScopedValueRollback>
#include <QWindow>
#include <QThreadPool>

#if HAVE_KWINDOWSYSTEM
#include <KWindowShadow>
#endif

#include <cmath>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
constexpr int RevealTimeoutMs = 160; // show even if content is not ready yet
constexpr int ResizeIdleEndMs = 1500;
constexpr int ShadowSize = 28;

int categoryIndex(SizeCategory c)
{
    return int(c);
}
} // namespace

// Paints the rounded corners transparent (antialiased) on top of all
// content, so every child can paint square without caring about the shape.
class CornerMask : public QWidget
{
public:
    explicit CornerMask(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (window()->isFullScreen() || window()->isMaximized()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath outside;
        outside.addRect(QRectF(rect()).adjusted(-1, -1, 1, 1));
        outside = outside.subtracted(Style::roundedRect(QRectF(rect()), Style::CornerRadius));
        p.setCompositionMode(QPainter::CompositionMode_Clear);
        p.fillPath(outside, Qt::black);
        // Hairline border helps the window stand out on similar backgrounds.
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlphaF(0.12);
        p.setPen(QPen(border, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(Style::roundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Style::CornerRadius));
    }
};

// Short message in the middle of the window ("Settings reset").
class Toast : public QWidget
{
public:
    explicit Toast(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, &QWidget::hide);
        hide();
    }
    void showMessage(const QString &text)
    {
        m_text = text;
        const QFontMetrics fm(font());
        resize(fm.horizontalAdvance(m_text) + 36, fm.height() + 20);
        move((parentWidget()->width() - width()) / 2, (parentWidget()->height() - height()) / 2);
        show();
        raise();
        update();
        m_timer.start(1600);
    }
    QString text() const
    {
        return m_text;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 185));
        p.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    QTimer m_timer;
};

// Invisible strip along an edge/corner that starts a compositor resize.
class ResizeHandle : public QWidget
{
public:
    ResizeHandle(Qt::Edges edges, PreviewWindow *window)
        : QWidget(window)
        , m_edges(edges)
        , m_window(window)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        const bool diagonalMain = (edges == (Qt::TopEdge | Qt::LeftEdge)) || (edges == (Qt::BottomEdge | Qt::RightEdge));
        const bool diagonalAnti = (edges == (Qt::TopEdge | Qt::RightEdge)) || (edges == (Qt::BottomEdge | Qt::LeftEdge));
        if (diagonalMain) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (diagonalAnti) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else {
            setCursor(Qt::SizeVerCursor);
        }
    }
    Qt::Edges edges() const
    {
        return m_edges;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || m_window->isFullScreen()) {
            event->ignore();
            return;
        }
        // Alt is captured at drag start; PreviewWindow chooses the photo/current-view ratio.
        const bool keepAspect = event->modifiers() & Qt::AltModifier;
        m_window->beginInteractiveResize(m_edges, keepAspect);
        if (QWindow *w = m_window->windowHandle(); !w || !w->startSystemResize(m_edges)) {
            m_window->endInteractiveResize();
        }
        event->accept();
    }

private:
    Qt::Edges m_edges;
    PreviewWindow *m_window;
};

PreviewWindow::PreviewWindow(Settings *settings, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_remote(new RemoteResolver(this))
    , m_settings(settings)
    , m_imageView(new ImageView(this))
    , m_mediaView(new MediaView(settings, this))
    , m_infoView(new InfoView(this))
    , m_textView(new TextView(this))
    , m_pdfView(new PdfView(this))
    , m_officeView(new OfficeView(this))
    , m_cornerMask(new CornerMask(this))
    , m_toast(new Toast(this))
{
    connect(m_remote, &RemoteResolver::resolved, this, [this](const QString &path) {
        const auto generation = m_itemGeneration;
        QPointer<PreviewWindow> guard(this);
        // MIME sniffing on a FUSE path can involve a network read. Keep it off
        // the GUI thread and reject it if the user closes or changes the file.
        QThreadPool::globalInstance()->start([path, generation, guard] {
            const auto type = detectFileType(path);
            QMetaObject::invokeMethod(qApp, [guard, path, generation, type] {
                if (!guard || !guard->m_sessionActive || guard->m_itemGeneration != generation) return;
                PreviewItem item = guard->m_item;
                item.path = path;
                item.type = type;
                guard->m_item = item;
                guard->loadLocalItem(item);
            }, Qt::QueuedConnection);
        });
    });
    connect(m_remote, &RemoteResolver::failed, this, &PreviewWindow::onViewFailed);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(Geometry::MinimumWindowSize);
    setWindowTitle(u"KDEGlance"_s);
    setObjectName(u"KDEGlancePreview"_s);

    for (ContentView *view : {static_cast<ContentView *>(m_imageView), static_cast<ContentView *>(m_mediaView), static_cast<ContentView *>(m_infoView), static_cast<ContentView *>(m_textView), static_cast<ContentView *>(m_pdfView), static_cast<ContentView *>(m_officeView)}) {
        view->hide();
        connect(view, &ContentView::navigateRequested, this, &PreviewWindow::navigateRequested);
        connect(view, &ContentView::closeRequested, this, &PreviewWindow::closePreview);
        connect(view, &ContentView::ready, this, [this, view] {
            if (view == m_current) {
                onViewReady();
            }
        });
        connect(view, &ContentView::failed, this, [this, view](const QString &message) {
            if (view == m_current) {
                onViewFailed(message);
            }
        });
        connect(view, &ContentView::naturalSizeChanged, this, [this, view] {
            if (view == m_current) {
                onNaturalSizeChanged();
            }
        });
        connect(view, &ContentView::sizeCategoryChanged, this, [this, view](SizeCategory c) {
            if (view == m_current) {
                onCategoryChanged(c);
            }
        });
    }
    m_imageView->setWheelResizesWindow(true);
    m_geometryDeadline.setSingleShot(true);
    m_geometryDeadline.setInterval(5000);
    connect(&m_geometryDeadline,&QTimer::timeout,this,[this] {
        if(!m_geometryRequest.isEmpty()) onViewFailed(tr_("The compositor did not confirm the window geometry"));
    });
    connect(m_imageView,&ImageView::coordinatedZoomRequested,this,&PreviewWindow::requestZoom);
    connect(m_imageView, &ImageView::wheelWindowPositionRequested, this,
            [this](const QPointF &position, qreal ratio, const QPointF &translation, const QPointF &anchor, bool physical) {
        if (m_current != m_imageView || m_resizing || isFullScreen() || isMaximized() || m_photoCompositorManaged) return;
        m_photoZoomPlacement = QRectF(position, size());
        m_photoZoomSteps.append(QJsonObject{{u"serial"_s, ++m_photoZoomSerial}, {u"ratio"_s, ratio},
            {u"tx"_s, translation.x()}, {u"ty"_s, translation.y()},
            {u"ax"_s, anchor.x()}, {u"ay"_s, anchor.y()}, {u"physical"_s, physical}});
        while (m_photoZoomSteps.size() > 256) m_photoZoomSteps.removeFirst();
        m_photoZoomClock.start();
        // Wayland placement is handled by our KWin helper. On X11/offscreen
        // QWidget can also apply the same request directly.
        if (QGuiApplication::platformName() != u"wayland"_s) move(position.toPoint());
    });
    connect(m_imageView, &ImageView::wheelDisplaySizeRequested, this, [this](const QSizeF& requested) {
        if(m_current!=m_imageView || m_resizing || isFullScreen() || isMaximized() || m_photoCompositorManaged || requested.isEmpty()) return;
        // While zooming out of a crop, keep the viewport until the entire
        // requested image fits. Only then does the window follow the image.
        if(requested.width()<m_imageView->displayRect().width()
            && (requested.width()>width() || requested.height()>height())) return;
        QSizeF target=requested;
        const QSize area=workArea().size();
        if(target.width()>area.width() || target.height()>area.height()) target.scale(QSizeF(area),Qt::KeepAspectRatio);
        resizeProgrammatically(target.toSize().expandedTo(QSize(1,1)), ResizeOrigin::Wheel);
    });
    connect(m_mediaView, &MediaView::fullscreenRequested, this, &PreviewWindow::toggleFullscreen);
    connect(m_infoView, &InfoView::playStreamRequested, this, [this](const QUrl &url, const QString &title) {
        switchTo(m_mediaView);
        m_mediaView->loadStream(url, title);
    });

    const std::array<Qt::Edges, 8> edges{Qt::Edges(Qt::LeftEdge),
                                         Qt::Edges(Qt::RightEdge),
                                         Qt::Edges(Qt::TopEdge),
                                         Qt::Edges(Qt::BottomEdge),
                                         Qt::TopEdge | Qt::LeftEdge,
                                         Qt::TopEdge | Qt::RightEdge,
                                         Qt::BottomEdge | Qt::LeftEdge,
                                         Qt::BottomEdge | Qt::RightEdge};
    for (size_t i = 0; i < edges.size(); ++i) {
        m_handles[i] = new ResizeHandle(edges[i], this);
    }

    m_revealTimer.setSingleShot(true);
    m_revealTimer.setInterval(RevealTimeoutMs);
    connect(&m_revealTimer, &QTimer::timeout, this, &PreviewWindow::revealIfPending);

    m_resizeEndTimer.setSingleShot(true);
    m_resizeEndTimer.setInterval(ResizeIdleEndMs);
    connect(&m_resizeEndTimer, &QTimer::timeout, this, &PreviewWindow::endInteractiveResize);

    m_middleClickTimer.setSingleShot(true);
    m_middleClickTimer.setInterval(QApplication::doubleClickInterval());
    connect(&m_middleClickTimer, &QTimer::timeout, this, [this] {
        closePreview(); // it was a single middle click
    });

    // See every mouse event inside the window (middle clicks, end of
    // interactive resizes) regardless of which child receives it.
    qApp->installEventFilter(this);
    applySettings();
}

PreviewWindow::~PreviewWindow()
{
    closePreview();
    qApp->removeEventFilter(this);
}

QSize PreviewWindow::defaultSizeFor(SizeCategory category)
{
    switch (category) {
    case SizeCategory::Pdf:
    case SizeCategory::Text:
    case SizeCategory::Office:
    case SizeCategory::Document: return QSize(800, 700);
    case SizeCategory::Audio:
        return MediaView::AudioWindowSize;
    case SizeCategory::Video:
        return QSize(960, 540);
    case SizeCategory::Image:
        return QSize(800, 600);
    }
    return QSize(800, 600);
}

void PreviewWindow::applySettings()
{
    m_imageView->setRawPreferEmbedded(m_settings->rawPreferEmbeddedPreview());
}

ContentView *PreviewWindow::currentView() const
{
    return m_current;
}

ImageView *PreviewWindow::imageView() const
{
    return m_imageView;
}

MediaView *PreviewWindow::mediaView() const
{
    return m_mediaView;
}

InfoView *PreviewWindow::infoView() const
{
    return m_infoView;
}

SizeCategory PreviewWindow::currentCategory() const
{
    return m_category;
}

bool PreviewWindow::aspectLockActive() const
{
    return m_resizing && m_aspectLock;
}

bool PreviewWindow::userResizedThisSession() const
{
    return m_userSized[size_t(categoryIndex(m_category))];
}

PreviewItem PreviewWindow::currentItem() const
{
    return m_item;
}

bool PreviewWindow::isPreviewVisible() const
{
    return m_sessionActive;
}

void PreviewWindow::setWorkArea(const QRect &area)
{
    if (!area.isValid() || area == m_workArea) {
        return;
    }
    m_workArea = area;
    updateDecodeTarget();
    if (isVisible() && !isFullScreen()) {
        // Resolution/scale changed: never be larger than the work area.
        const QSize clamped = Geometry::clampToArea(size(), m_workArea.size());
        if (clamped != size()) {
            resizeProgrammatically(clamped);
        }
    }
}

QRect PreviewWindow::workArea() const
{
    if (m_workArea.isValid()) {
        return m_workArea;
    }
    // Without KWin's report: the screen geometry (panels not excluded).
    QScreen *screen = windowHandle() && windowHandle()->screen() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
}

void PreviewWindow::updateDecodeTarget()
{
    // Decode images for the largest window we would open automatically or
    // the remembered size, whichever is larger.
    const QRect area = workArea();
    QSize logical(int(area.width() * m_settings->autoSizeFraction()), int(area.height() * m_settings->autoSizeFraction()));
    if (auto saved = m_settings->savedSize(SizeCategory::Image)) {
        logical = logical.expandedTo(*saved);
    }
    const qreal dpr = windowHandle() && windowHandle()->screen() ? windowHandle()->screen()->devicePixelRatio()
                                                                 : (QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0);
    m_imageView->setDecodeTarget((QSizeF(logical) * dpr).toSize());
}

void PreviewWindow::prefetch(const QList<PreviewItem> &items)
{
    m_imageView->prefetch(items);
}

void PreviewWindow::showItem(const PreviewItem &item, bool newSession)
{
    cancelGeometry();
    m_previousFrame = isVisible() && m_current && m_current->isVisible() ? m_current->grab() : QPixmap();
    m_contentReady = false;
    m_staging = true;
    if(m_current) m_current->hide();
    m_photoZoomPlacement = {};
    m_photoZoomSteps = {};
    if (item.url != m_item.url) {
        // A free resize belongs to the current file. The next photo/video
        // starts aspect-correct inside the same remembered bounds.
        m_userSized[size_t(categoryIndex(SizeCategory::Image))] = false;
        m_userSized[size_t(categoryIndex(SizeCategory::Video))] = false;
    }
    ++m_itemGeneration;
    m_middleClickTimer.stop();
    if (m_current) m_current->unload();
    m_remote->cancel();
    m_item = item;
    if (newSession || !m_sessionActive) {
        m_sessionActive = true;
        m_userSized.fill(false);
        m_wasActive = false;
        m_photoCompositorManaged = false;
        const bool photo=item.type.kind==ContentKind::Image || item.type.kind==ContentKind::Raw || item.type.kind==ContentKind::Svg;
        if(photo && isMaximized()) setWindowState(windowState() & ~Qt::WindowMaximized);
        if (isFullScreen()) {
            showNormal();
        }
        m_revealPending = !isVisible();
        updateDecodeTarget();
    }
    setMinimumSize((item.type.kind == ContentKind::Image || item.type.kind == ContentKind::Svg || item.type.kind == ContentKind::Raw || item.type.kind == ContentKind::Video) ? QSize(1, 1) : Geometry::MinimumWindowSize);
    setWindowTitle(QFileInfo(item.path).fileName() + u" — KDEGlance"_s);

    if (!item.url.isLocalFile() && item.path.isEmpty()) {
        m_staging = false; // a loading card is deliberate, not partial media
        switchTo(m_infoView);
        m_infoView->showLoading();
        m_category = sizeCategoryFor(item.type.kind);
        resizeProgrammatically(defaultSizeFor(m_category).boundedTo(workArea().size()));
        revealIfPending();
        m_remote->resolve(item.url);
        return;
    }
    loadLocalItem(item);
}

void PreviewWindow::loadLocalItem(const PreviewItem &item)
{
    m_staging = item.type.kind==ContentKind::Image || item.type.kind==ContentKind::Raw
        || item.type.kind==ContentKind::Svg || item.type.kind==ContentKind::Video;
    m_contentReady = false;
    if(m_staging && m_current) m_current->hide();
    setMinimumSize((item.type.kind == ContentKind::Image || item.type.kind == ContentKind::Svg || item.type.kind == ContentKind::Raw || item.type.kind == ContentKind::Video)
                       ? QSize(1, 1) : Geometry::MinimumWindowSize);
    ContentView *target = m_infoView;
    switch (item.type.kind) {
    case ContentKind::Office: target = m_officeView; break;
    case ContentKind::Text: target = m_textView; break;
    case ContentKind::Pdf: target = m_pdfView; break;
    case ContentKind::Image:
    case ContentKind::Svg:
    case ContentKind::Raw:
        target = m_imageView;
        break;
    case ContentKind::Video:
    case ContentKind::Audio:
    case ContentKind::Midi:
        target = m_mediaView;
        break;
    default:
        target = m_infoView;
        break;
    }
    const auto previousCategory=m_category;
    switchTo(target);
    if (item.type.kind == ContentKind::Missing) {
        m_infoView->showError(item, tr_("The file no longer exists or cannot be read"));
        m_category = SizeCategory::Image;
        applyTargetSize(true);
        revealIfPending();
        return;
    }
    m_category = sizeCategoryFor(item.type.kind);
    if(target==m_imageView) m_imageView->setFitUpscale(m_settings->savedSize(SizeCategory::Image).has_value());
    target->load(item);

    // A different module must restore its own geometry, even if it was resized
    // earlier in this navigation session.
    applyTargetSize(previousCategory!=m_category);
    if (m_revealPending) {
        m_revealTimer.start();
    }
}

void PreviewWindow::switchTo(ContentView *view)
{
    if (m_current == view) {
        if(!m_staging) m_current->show();
        return;
    }
    if (m_current) {
        m_current->unload();
        m_current->hide();
    }
    m_current = view;
    m_current->setGeometry(rect());
    if(!m_staging) m_current->show();
    layoutOverlays();
}

void PreviewWindow::setCoordinatedGeometry(bool enabled)
{
    m_coordinatedGeometry=enabled;
    m_imageView->setCoordinatedZoom(enabled);
    if(!enabled && !m_geometryRequest.isEmpty()) cancelGeometry();
}

void PreviewWindow::cancelGeometry()
{
    m_geometryDeadline.stop();
    m_geometryRequest={};
    m_geometryIssued=false;
    m_desiredZoom=0;
    m_queuedWheel=false;
    m_imageView->holdGeometryZoom(false);
    setUpdatesEnabled(true);
}

void PreviewWindow::presentContent()
{
    m_staging=false;
    m_previousFrame={};
    m_current->setGeometry(rect());
    if(m_current==m_imageView) m_imageView->fitToWindow();
    m_current->show();
    layoutOverlays();
    setUpdatesEnabled(true);
    updateShadow();
    revealIfPending();
    update();
    Q_EMIT currentContentReady();
}

void PreviewWindow::requestInitialGeometry()
{
    // Navigation inside compositor-managed geometry must fit that viewport,
    // not wait for a normal-window configure that KWin cannot issue there.
    if(isFullScreen() || isMaximized() || m_photoCompositorManaged) {
        presentContent();
        return;
    }
    const QSize target=targetSizeFor(m_category,m_current->naturalSizeF());
    if(!target.isValid()) return;
    if(m_geometryRequest.value(u"kind"_s)==u"initial"_s
        && m_geometryRequest.value(u"width"_s).toInt()==target.width()
        && m_geometryRequest.value(u"height"_s).toInt()==target.height()) return;
    m_geometryIssued=false;
    m_geometryRequest={{u"id"_s,QString::number(++m_geometrySerial)},{u"kind"_s,u"initial"_s},
        {u"visual"_s,m_current==m_imageView || (m_current==m_mediaView && m_mediaView->hasVideo())},
        {u"width"_s,target.width()},{u"height"_s,target.height()}};
    m_geometryDeadline.start();
    if(!isVisible()) {
        resizeProgrammatically(target);
        m_current->setGeometry(rect());
        // Map a transparent surface for the Wayland configure handshake.
        // No image/video buffer is exposed until the compositor has issued it.
        show();
    }
    Q_EMIT geometryRequested();
}

void PreviewWindow::requestZoom(qreal factor,const QPointF &anchor,bool physical)
{
    if(m_current!=m_imageView || m_staging || m_resizing) return;
    m_desiredZoom=m_imageView->boundedScale((m_desiredZoom>0?m_desiredZoom:m_imageView->zoomScale())*factor);
    m_zoomAnchor=anchor;
    m_zoomPhysical=physical;
    m_queuedWheel=true;
    m_anchorAfterResize=!m_geometryRequest.isEmpty() && size()==QSize(m_geometryRequest.value(u"width"_s).toInt(),m_geometryRequest.value(u"height"_s).toInt());
    if(m_geometryRequest.isEmpty()) dispatchZoom();
}

void PreviewWindow::dispatchZoom()
{
    if(!m_queuedWheel || m_desiredZoom<=0) return;
    m_queuedWheel=false;
    const Geometry::ZoomState before{m_imageView->zoomScale(),m_imageView->zoomOffset()};
    const QSizeF units=m_imageView->naturalSizeF();
    const QSizeF requested=units*m_desiredZoom;
    QSize target=size();
    if(!isFullScreen() && !isMaximized() && !m_photoCompositorManaged
        && !(requested.width()<m_imageView->displayRect().width() && (requested.width()>width() || requested.height()>height()))) {
        target=requested.scaled(requested.boundedTo(QSizeF(workArea().size())),Qt::KeepAspectRatio).toSize().expandedTo(QSize(1,1));
    }
    if(target==size()) {
        m_imageView->zoomAt(m_desiredZoom,m_zoomAnchor);
        m_desiredZoom=0;
        return;
    }
    m_geometryZoom=Geometry::zoomAt(before,units,target,m_desiredZoom,m_zoomAnchor);
    const qreal ratio=m_geometryZoom.scale/before.scale;
    const QPointF translation=m_geometryZoom.offset-before.offset*ratio;
    m_imageView->holdGeometryZoom(true);
    m_geometryIssued=false;
    m_geometryRequest={{u"id"_s,QString::number(++m_geometrySerial)},{u"kind"_s,u"zoom"_s},
        {u"width"_s,target.width()},{u"height"_s,target.height()},{u"ratio"_s,ratio},
        {u"tx"_s,translation.x()},{u"ty"_s,translation.y()},
        {u"ax"_s,m_zoomAnchor.x()},{u"ay"_s,m_zoomAnchor.y()},{u"physical"_s,m_zoomPhysical}};
    m_geometryDeadline.start();
    Q_EMIT geometryRequested();
}

void PreviewWindow::geometryIssued(const QString &id,qreal dx,qreal dy)
{
    if(m_geometryRequest.value(u"id"_s).toString()!=id) return;
    m_geometryIssued=true;
    m_geometryDelta={dx,dy};
    tryCommitGeometry();
}

void PreviewWindow::geometryRejected(const QString &id)
{
    if(m_geometryRequest.value(u"id"_s).toString()!=id) return;
    cancelGeometry();
    if(m_current==m_imageView && m_imageView->hasImage()) {
        if(m_imageView->isFitMode()) m_imageView->fitToWindow();
        else m_imageView->zoomAt(m_imageView->zoomScale(),QRectF(rect()).center());
    }
}

void PreviewWindow::discardPhotoZoomPlacement()
{
    m_photoZoomPlacement={};m_photoZoomSteps={};
    if(m_geometryRequest.value(u"kind"_s)==u"zoom"_s)
        geometryRejected(m_geometryRequest.value(u"id"_s).toString());
}

void PreviewWindow::tryCommitGeometry()
{
    if(!m_geometryIssued || m_geometryRequest.isEmpty() || !isVisible()
        || (windowHandle() && !windowHandle()->isExposed())) return;
    const QSize target(m_geometryRequest.value(u"width"_s).toInt(),m_geometryRequest.value(u"height"_s).toInt());
    if(size()!=target) return;
    const bool initial=m_geometryRequest.value(u"kind"_s)==u"initial"_s;
    m_geometryDeadline.stop();
    m_geometryRequest={};
    m_geometryIssued=false;
    if(initial) presentContent();
    else {
        m_imageView->commitGeometryZoom(m_geometryZoom);
        setUpdatesEnabled(true);
        if(m_queuedWheel) {
            if(!m_anchorAfterResize) m_zoomAnchor-=m_geometryDelta;
            QTimer::singleShot(0,this,[this]{if(m_geometryRequest.isEmpty()) dispatchZoom();});
        } else m_desiredZoom=0;
    }
}

void PreviewWindow::closePreview()
{
    cancelGeometry();
    m_staging=false;
    m_previousFrame={};
    m_photoZoomPlacement = {};
    m_photoZoomSteps = {};
    ++m_itemGeneration;
    m_middleClickTimer.stop();
    m_revealTimer.stop();
    if (m_resizing) {
        endInteractiveResize();
    }
    m_mediaView->unload(); // also stop a hidden player, including pending source changes
    if (m_current) {
        m_current->unload(); // stops playback and frees decoders right away
    }
    m_remote->cancel();
    const bool wasActive = m_sessionActive;
    m_sessionActive = false;
    m_revealPending = false;
    if (isFullScreen()) {
        showNormal();
    }
    hide();
    if (wasActive) {
        Q_EMIT closed();
    }
}

void PreviewWindow::onViewReady()
{
    m_contentReady=true;
    if(m_staging) {
        if(m_coordinatedGeometry) requestInitialGeometry();
        else { applyTargetSize(false); presentContent(); }
        return;
    }
    applyTargetSize(false);
    revealIfPending();
    Q_EMIT currentContentReady();
}

void PreviewWindow::onViewFailed(const QString &message)
{
    cancelGeometry();
    m_staging=false;
    m_contentReady=true;
    m_previousFrame={};
    const PreviewItem item = m_item;
    switchTo(m_infoView);
    m_infoView->showError(item, message);
    m_category = SizeCategory::Image;
    applyTargetSize(false);
    revealIfPending();
    Q_EMIT currentContentReady();
}

void PreviewWindow::onNaturalSizeChanged()
{
    if(m_staging && isVisible() && m_coordinatedGeometry) {
        if(m_contentReady) requestInitialGeometry();
        return;
    }
    applyTargetSize(false);
}

void PreviewWindow::onCategoryChanged(SizeCategory category)
{
    if (category == m_category) {
        return;
    }
    m_category = category;
    if(!m_staging) applyTargetSize(true);
}

QSize PreviewWindow::targetSizeFor(SizeCategory category, const QSizeF &natural) const
{
    const QSize area = workArea().size();
    if (auto saved = m_settings->savedSize(category)) {
        const QSize bounds = Geometry::clampToArea(*saved, area, m_current==m_imageView?QSize(1,1):Geometry::MinimumWindowSize);
        if (natural.isValid() && (m_current == m_imageView || category == SizeCategory::Video)) {
            const QSizeF fitted = natural.scaled(QSizeF(bounds), Qt::KeepAspectRatio);
            return m_current == m_imageView ? fitted.toSize().expandedTo(QSize(1,1))
                                           : Geometry::autoWindowSize(fitted, area, 1.0);
        }
        return bounds;
    }
    if (m_current == m_imageView && natural.isValid()) {
        return natural.scaled(natural.boundedTo(QSizeF(area)), Qt::KeepAspectRatio).toSize().expandedTo(QSize(1, 1));
    }
    if (m_current == m_infoView) {
        return Geometry::clampToArea(m_infoView->naturalSize(), area);
    }
    if (category == SizeCategory::Document || category == SizeCategory::Pdf || category == SizeCategory::Text || category == SizeCategory::Office) return Geometry::clampToArea(defaultSizeFor(category), area);
    if (category == SizeCategory::Audio) {
        return Geometry::clampToArea(MediaView::AudioWindowSize, area);
    }
    if (!natural.isValid()) {
        return QSize();
    }
    return Geometry::autoWindowSize(natural, area, m_settings->autoSizeFraction());
}

void PreviewWindow::applyTargetSize(bool force)
{
    if(m_staging && m_coordinatedGeometry && isVisible()) return;
    if (isFullScreen() || m_resizing || (m_current==m_imageView && (isMaximized() || m_photoCompositorManaged))) {
        return;
    }
    // Within a session a size the user chose is kept for this category.
    if (!force && m_userSized[size_t(categoryIndex(m_category))]) {
        return;
    }
    QSize target = targetSizeFor(m_category, m_current ? m_current->naturalSizeF() : QSizeF());
    if (!target.isValid()) {
        if (!isVisible()) {
            target = Geometry::clampToArea(defaultSizeFor(m_category), workArea().size());
        } else {
            return; // wait for the natural size
        }
    }
    if (target != size()) {
        resizeProgrammatically(target);
    }
}

void PreviewWindow::resizeProgrammatically(const QSize &size, ResizeOrigin origin)
{
    const QScopedValueRollback<ResizeOrigin> source(m_resizeOrigin, origin);
    resize(size);
}

void PreviewWindow::revealIfPending()
{
    if(m_staging) return;
    if (!m_revealPending || !m_sessionActive) {
        return;
    }
    m_revealPending = false;
    m_revealTimer.stop();
    if (!size().isValid() || size() == QSize(0, 0)) {
        resizeProgrammatically(defaultSizeFor(m_category));
    }
    show();
    raise();
    activateWindow();
    m_shownClock.start();
}

void PreviewWindow::setPhotoCompositorManaged(bool managed)
{
    m_photoCompositorManaged = managed;
    if(m_current==m_imageView && !m_resizing)
        m_resizeOrigin=managed ? ResizeOrigin::Compositor : ResizeOrigin::Automatic;
    if(managed && m_current==m_imageView) {
        discardPhotoZoomPlacement();
        // A compositor transition is never a manually chosen persistent size.
        m_resizing=false;
        m_resizeOrigin=ResizeOrigin::Compositor;
        m_aspectLock=false;
        m_resizeEndTimer.stop();
    }
}

void PreviewWindow::beginInteractiveResize(Qt::Edges edges, bool keepAspect)
{
    m_resizing = true;
    m_resizeOrigin = ResizeOrigin::Manual;
    m_aspectLock = keepAspect;
    m_resizeEdges = edges;
    m_resizeStartSize = size();
    m_resizeRatio = m_resizeStartSize;
    if(m_current==m_imageView) {
        if(keepAspect && m_imageView->naturalSize().isValid()) m_resizeRatio=m_imageView->naturalSize();
        m_imageView->prepareWindowResize();
    }
    m_resizeClock.start();
    m_resizeEndTimer.start();
}

void PreviewWindow::endInteractiveResize()
{
    if (!m_resizing) {
        return;
    }
    m_resizing = false;
    m_aspectLock = false;
    m_resizeEndTimer.stop();
    if (m_resizeOrigin==ResizeOrigin::Manual && size() != m_resizeStartSize && !isFullScreen() && !(m_current==m_imageView && (isMaximized() || m_photoCompositorManaged))) {
        // Only sizes the user chose are remembered (spec 5.4).
        m_userSized[size_t(categoryIndex(m_category))] = true;
        m_settings->setSavedSize(m_category, size());
        Q_EMIT settingsModified();
    }
    m_resizeOrigin=ResizeOrigin::Automatic;
}

void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_resizing && m_resizeOrigin==ResizeOrigin::Manual && !(m_current==m_imageView && (isMaximized() || m_photoCompositorManaged))) {
        m_resizeEndTimer.start();
        if (m_aspectLock) {
            Qt::Edges driving=m_resizeEdges;
            if(m_current==m_imageView && (driving & (Qt::LeftEdge|Qt::RightEdge)) && (driving & (Qt::TopEdge|Qt::BottomEdge))) {
                const double dw=std::abs(double(width())/m_resizeStartSize.width()-1);
                const double dh=std::abs(double(height())/m_resizeStartSize.height()-1);
                driving &= dw>=dh ? (Qt::LeftEdge|Qt::RightEdge) : (Qt::TopEdge|Qt::BottomEdge);
            }
            const QSize locked = Geometry::aspectLockedSize(m_resizeRatio, size(), driving, minimumSize(),
                                                            m_workArea.isValid() ? m_workArea.size() : QSize());
            if (locked != size()) {
                // Answer the compositor's proposal with the ratio-locked size;
                // KWin keeps the edge opposite to the dragged one in place.
                resizeProgrammatically(locked);
                return;
            }
        }
    }
    if (m_current) {
        m_current->setGeometry(rect());
    }
    layoutOverlays();
    if(!m_geometryRequest.isEmpty() && isVisible()
        && (m_geometryRequest.value(u"kind"_s)==u"zoom"_s || !m_previousFrame.isNull())) {
        if(!m_geometryIssued) setUpdatesEnabled(false);
        tryCommitGeometry();
    }
}

void PreviewWindow::layoutOverlays()
{
    if (m_current) {
        m_current->setGeometry(rect());
    }
    m_cornerMask->setGeometry(rect());
    const int b = Style::ResizeBorder;
    const int c = Style::ResizeCorner;
    const int w = width();
    const int h = height();
    const bool enabled = !isFullScreen();
    // Order: left, right, top, bottom, tl, tr, bl, br
    m_handles[0]->setGeometry(0, c, b, h - 2 * c);
    m_handles[1]->setGeometry(w - b, c, b, h - 2 * c);
    m_handles[2]->setGeometry(c, 0, w - 2 * c, b);
    m_handles[3]->setGeometry(c, h - b, w - 2 * c, b);
    m_handles[4]->setGeometry(0, 0, c, b);
    m_handles[5]->setGeometry(w - c, 0, c, b);
    m_handles[6]->setGeometry(0, h - b, c, b);
    m_handles[7]->setGeometry(w - c, h - b, c, b);
    // Corner handles also cover the short vertical part of the corner.
    for (QWidget *handle : m_handles) {
        handle->setVisible(enabled && !m_staging);
        handle->raise();
    }
    m_cornerMask->setVisible(!m_staging);
    m_cornerMask->raise();
    if (m_toast->isVisible()) {
        m_toast->move((w - m_toast->width()) / 2, (h - m_toast->height()) / 2);
        m_toast->raise();
    }
}

void PreviewWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if(m_staging) {
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(),Qt::transparent);
        if(!m_previousFrame.isNull()) p.drawPixmap(QPoint(),m_previousFrame);
        return;
    }
    p.setRenderHint(QPainter::Antialiasing);
    const qreal radius = (isFullScreen() || isMaximized()) ? 0.0 : Style::CornerRadius;
    p.setPen(Qt::NoPen);
    p.setBrush(Style::windowBackground(this));
    p.drawRoundedRect(QRectF(rect()), radius, radius);
}

void PreviewWindow::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if ((key == Qt::Key_Return || key == Qt::Key_Enter)
        && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        if (!event->isAutoRepeat() && m_sessionActive) {
            const QUrl original = m_item.url;
            closePreview(); // stop playback and cancel loads before launching
            Q_EMIT openExternallyRequested(original);
        }
        event->accept();
        return;
    }
    const QKeySequence hotkey = m_settings->shortcut();
    const bool isHotkey = !hotkey.isEmpty() && QKeyCombination(event->modifiers() & ~Qt::KeypadModifier, Qt::Key(key)) == hotkey[0];
    if (key == Qt::Key_Escape || (key == Qt::Key_Space && event->modifiers() == Qt::NoModifier) || isHotkey) {
        if (!event->isAutoRepeat()) {
            if (key == Qt::Key_Escape && isFullScreen()) {
                toggleFullscreen();
            } else {
                closePreview();
            }
        }
        event->accept();
        return;
    }
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        Q_EMIT navigateRequested(key == Qt::Key_Left ? -1 : 1);
        event->accept();
        return;
    }
    if (m_current && m_current->handleKey(event)) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PreviewWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            m_wasActive = true;
        } else if (m_sessionActive && isVisible() && m_wasActive && m_settings->closeOnFocusLoss()) {
            const bool ignore = m_ignoreDeactivateClock.isValid() && m_ignoreDeactivateClock.elapsed() < 400;
            if (!ignore && !m_resizing) {
                // Deferred: do not tear the window down inside event delivery.
                QTimer::singleShot(0, this, [this] {
                    if (m_sessionActive && !isActiveWindow()) {
                        closePreview();
                    }
                });
            }
        }
    } else if (event->type() == QEvent::WindowStateChange) {
        if(m_current==m_imageView && isMaximized()) {
            m_resizing=false;
            m_aspectLock=false;
            m_resizeEndTimer.stop();
        }
        layoutOverlays();
        update();
    } else if (event->type() == QEvent::PaletteChange) {
        update();
    }
}

void PreviewWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if(windowHandle()) windowHandle()->installEventFilter(this);
    layoutOverlays();
    if(!m_staging) updateShadow();
}

QRectF PreviewWindow::photoZoomPlacement() const
{
    if (!m_sessionActive || m_current != m_imageView || m_resizing || isFullScreen()
        || isMaximized() || m_photoCompositorManaged || !m_photoZoomClock.isValid()
        || m_photoZoomClock.elapsed() > 1000 || m_photoZoomPlacement.size() != QSizeF(size())) return {};
    return m_photoZoomPlacement;
}

void PreviewWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void PreviewWindow::closeEvent(QCloseEvent *event)
{
    // Closing via the compositor (e.g. Alt+F4) ends the session like Escape.
    // The window object itself is kept for the next preview.
    closePreview();
    event->accept();
}

void PreviewWindow::updateShadow()
{
#if HAVE_KWINDOWSYSTEM
    if (!windowHandle()) {
        return;
    }
    if (!m_shadow) {
        m_shadow = new KWindowShadow(this);
        // Render a blurred rounded rectangle and cut it into nine tiles.
        const int s = ShadowSize;
        const int r = int(Style::CornerRadius);
        const int side = 2 * (s + r) + 1;
        QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            for (int i = s; i > 0; --i) {
                const qreal alpha = 0.32 * std::pow(1.0 - qreal(i) / s, 2.0) / 3.0;
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0, 0, 0, int(alpha * 255)));
                const QRectF rr(s - i, s - i + 4, side - 2 * (s - i), side - 2 * (s - i) - 4);
                p.drawRoundedRect(rr, r + i, r + i);
            }
            // Nothing under the window itself.
            p.setCompositionMode(QPainter::CompositionMode_Clear);
            p.drawRoundedRect(QRectF(s, s, side - 2 * s, side - 2 * s), r, r);
        }
        auto tile = [&img](int x, int y, int w, int h) {
            auto t = KWindowShadowTile::Ptr::create();
            t->setImage(img.copy(x, y, w, h));
            return t;
        };
        const int a = s + r;
        m_shadow->setTopLeftTile(tile(0, 0, a, a));
        m_shadow->setTopTile(tile(a, 0, 1, a));
        m_shadow->setTopRightTile(tile(a + 1, 0, a, a));
        m_shadow->setLeftTile(tile(0, a, a, 1));
        m_shadow->setRightTile(tile(a + 1, a, a, 1));
        m_shadow->setBottomLeftTile(tile(0, a + 1, a, a));
        m_shadow->setBottomTile(tile(a, a + 1, 1, a));
        m_shadow->setBottomRightTile(tile(a + 1, a + 1, a, a));
        m_shadow->setPadding(QMargins(s, s, s, s));
    }
    // KWindowShadow rejects setWindow while native resources still exist.
    m_shadow->destroy();
    m_shadow->setWindow(windowHandle());
    m_shadow->create();
#endif
}

void PreviewWindow::toggleFullscreen()
{
    m_ignoreDeactivateClock.start();
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void PreviewWindow::handleMiddleClick()
{
    if (m_middleClickTimer.isActive()) {
        m_middleClickTimer.stop();
        resetActiveModule();
        return;
    }
    m_middleClickTimer.start();
}

void PreviewWindow::resetActiveModule()
{
    if(!m_current || m_current==m_infoView) return;
    m_settings->resetModule(m_category);
    m_userSized[size_t(categoryIndex(m_category))]=false;
    applySettings();
    m_current->resetPresentation();
    Q_EMIT settingsResetRequested();
    updateDecodeTarget();
    applyTargetSize(true);
    m_toast->showMessage(tr_("Active module settings were reset to defaults"));
}

bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    if(watched==windowHandle() && event->type()==QEvent::Expose) tryCommitGeometry();
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this) {
        return false;
    }
    switch (event->type()) {
    case QEvent::Wheel:
        // The compositor may return the pointer without a final mouse move.
        // Commit the real resize before processing a subsequent temporary zoom.
        if(m_current==m_imageView && m_resizing && m_resizeClock.elapsed()>100 && QGuiApplication::mouseButtons()==Qt::NoButton)
            endInteractiveResize();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_resizing && m_resizeClock.elapsed() > 100) {
            endInteractiveResize();
        }
        if (me->button() == Qt::MiddleButton) {
            handleMiddleClick();
            return true;
        }
        break;
    }
    case QEvent::MouseMove:
    case QEvent::Enter:
        // The compositor gives the pointer back when an interactive resize ends.
        if (m_resizing && m_resizeClock.elapsed() > 100 && QGuiApplication::mouseButtons() == Qt::NoButton) {
            endInteractiveResize();
        }
        break;
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
                keyPressEvent(key); return true;
            }
        }
        if (m_current == m_textView || m_current == m_pdfView || m_current == m_officeView) {
            if (key->modifiers()==Qt::NoModifier && (key->key()==Qt::Key_Space || key->key()==Qt::Key_Escape
                || key->key()==Qt::Key_Left || key->key()==Qt::Key_Right)) { keyPressEvent(key); return true; }
            if (m_current->handleKey(key)) return true;
            // No permanent toolbar: retain normal keyboard document scrolling.
            if (key->modifiers()==Qt::NoModifier) {
                for (auto* area : m_current->findChildren<QAbstractScrollArea*>()) if (area->isVisible()) {
                    auto* bar=area->verticalScrollBar();
                    switch (key->key()) {
                    case Qt::Key_Down: bar->setValue(bar->value()+bar->singleStep()); return true;
                    case Qt::Key_Up: bar->setValue(bar->value()-bar->singleStep()); return true;
                    case Qt::Key_PageDown: bar->setValue(bar->value()+bar->pageStep()); return true;
                    case Qt::Key_PageUp: bar->setValue(bar->value()-bar->pageStep()); return true;
                    case Qt::Key_Home: bar->setValue(bar->minimum()); return true;
                    case Qt::Key_End: bar->setValue(bar->maximum()); return true;
                    default: break;
                    }
                }
            }
        }
        if (m_resizing && m_resizeClock.elapsed() > 100) {
            endInteractiveResize();
        }
        break;
    }
    default:
        break;
    }
    return false;
}

} // namespace KDEGlance
