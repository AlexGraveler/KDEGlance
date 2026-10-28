// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "imageview.h"

#include "animationplayer.h"
#include "imageloader.h"
#include "l10n.h"
#include "style.h"

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
constexpr int CacheSize = 4;
constexpr int SpinnerDelayMs = 250;
constexpr qreal AbsoluteMaxScale = 32.0;
} // namespace

ImageView::ImageView(QWidget *parent)
    : ContentView(parent)
    , m_loader(new ImageLoader(this))
    , m_animation(new AnimationPlayer(this))
    , m_rotateButton(new OverlayButton(int(Style::Glyph::RotateLeft), this))
    , m_flipButton(new OverlayButton(int(Style::Glyph::Flip), this))
{
    setObjectName(QStringLiteral("imageView"));
    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        const QRectF image(m_zoom.offset, rotatedUnits() * m_zoom.scale);
        contentClick(m_clickPosition, image.intersected(QRectF(rect())));
    });

    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFocusPolicy(Qt::NoFocus);

    connect(m_loader, &ImageLoader::probeFinished, this, &ImageView::onProbeFinished);
    connect(m_loader, &ImageLoader::decodeFinished, this, &ImageView::onDecodeFinished);

    connect(m_animation, &AnimationPlayer::frameChanged, this, qOverload<>(&QWidget::update));
    connect(m_animation, &AnimationPlayer::firstFrame, this, [this](const QSize &size) {
        if (!m_natural.isValid()) {
            m_natural = size;
            Q_EMIT naturalSizeChanged(naturalSize());
        }
        m_loading = false;
        m_spinnerTimer.stop();
        fitToWindow();
        if (!m_ready) {
            m_ready = true;
            Q_EMIT ready();
        }
    });
    connect(m_animation, &AnimationPlayer::failed, this, &ImageView::fail);

    m_spinnerTimer.setInterval(16);
    connect(&m_spinnerTimer, &QTimer::timeout, this, [this] {
        m_spinnerPhase = std::fmod(m_spinnerPhase + 0.016, 1.0);
        if (m_loadClock.elapsed() >= SpinnerDelayMs) {
            update();
        }
    });

    // Requests for a sharper version are delayed until zooming settles.
    m_betterTimer.setSingleShot(true);
    m_betterTimer.setInterval(120);
    connect(&m_betterTimer, &QTimer::timeout, this, &ImageView::requestBetterResolution);

    m_rotateButton->setToolTip(tr_("Rotate left"));
    m_flipButton->setToolTip(tr_("Flip horizontally"));
    connect(m_rotateButton, &QAbstractButton::clicked, this, &ImageView::rotateLeft);
    connect(m_flipButton, &QAbstractButton::clicked, this, &ImageView::toggleMirror);
    setButtonsVisible(false);
}

ImageView::~ImageView()
{
    unload();
}

void ImageView::setRawPreferEmbedded(bool prefer)
{
    m_rawPreferEmbedded = prefer;
    m_loader->setRawPreferEmbedded(prefer);
}

void ImageView::setDecodeTarget(const QSize &devicePixels)
{
    if (devicePixels.isValid() && !devicePixels.isEmpty()) {
        m_decodeTarget = devicePixels;
    }
}

qint64 ImageView::mtimeOf(const QString &path) const
{
    return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

QSize ImageView::targetFor(const QSize &natural) const
{
    if (!natural.isValid()) {
        return m_decodeTarget;
    }
    return natural.scaled(m_decodeTarget, Qt::KeepAspectRatio).boundedTo(natural);
}

void ImageView::load(const PreviewItem &item)
{
    unload();
    m_item = item;
    m_token = makeCancelToken();
    m_loading = true;
    m_loadClock.start();
    m_spinnerTimer.start();

    // Decoded in advance?
    const qint64 mtime = mtimeOf(item.path);
    for (int i = 0; i < m_cache.size(); ++i) {
        const CacheEntry &e = m_cache.at(i);
        if (e.path == item.path && e.mtime == mtime && e.target == m_decodeTarget) {
            CacheEntry entry = m_cache.takeAt(i);
            m_cache.prepend(entry);
            m_probe = entry.probe;
            m_natural = entry.probe.naturalSize.isValid() ? entry.probe.naturalSize : entry.image.naturalSize;
            Q_EMIT naturalSizeChanged(naturalSize());
            applyDecoded(entry.image);
            return;
        }
    }
    m_probeId = m_loader->probe(item.path, item.type.kind, m_token);
}

void ImageView::unload()
{
    if (m_token) {
        m_token->store(true);
    }
    m_clickTimer.stop();
    m_token.reset();
    m_animation->stop();
    m_probeId = m_decodeId = m_betterId = 0;
    m_probe = ImageProbe();
    m_natural = QSize();
    m_display = QImage();
    m_full = QImage();
    m_displayIsFull = false;
    m_betterPending = false;
    m_svgRendered = QSize();
    m_loading = false;
    m_ready = false;
    m_page = 0;
    m_zoom = Geometry::ZoomState();
    m_fit = true;
    m_rotation = 0;
    m_mirror = false;
    m_panning = false;
    m_dragCandidate = false;
    m_spinnerTimer.stop();
    m_betterTimer.stop();
    update();
}

void ImageView::prefetch(const QList<PreviewItem> &items)
{
    // Cancel prefetches that are no longer neighbours.
    for (Prefetch &p : m_prefetch) {
        p.token->store(true);
    }
    m_prefetch.clear();
    for (const PreviewItem &item : items) {
        if (item.type.kind != ContentKind::Image && item.type.kind != ContentKind::Raw) {
            continue;
        }
        const qint64 mtime = mtimeOf(item.path);
        const bool cached = std::any_of(m_cache.cbegin(), m_cache.cend(), [&](const CacheEntry &e) {
            return e.path == item.path && e.mtime == mtime && e.target == m_decodeTarget;
        });
        if (cached || item.path == m_item.path) {
            continue;
        }
        Prefetch p;
        p.item = item;
        p.token = makeCancelToken();
        p.probeId = m_loader->probe(item.path, item.type.kind, p.token);
        m_prefetch.append(p);
    }
}

void ImageView::onProbeFinished(quint64 id, const ImageProbe &probe)
{
    // Prefetch pipeline: probe → decode (static single images only).
    for (Prefetch &p : m_prefetch) {
        if (p.probeId == id) {
            if (probe.ok && !probe.animated && probe.pageCount == 1 && probe.format != "svg") {
                p.probe = probe;
                p.target = m_decodeTarget;
                p.decodeId = m_loader->decode(p.item.path, p.item.type.kind, targetFor(probe.naturalSize), 0, p.token);
            }
            return;
        }
    }
    if (id != m_probeId) {
        return;
    }
    if (!probe.ok) {
        fail(probe.error);
        return;
    }
    m_probe = probe;
    if (probe.naturalSize.isValid()) {
        m_natural = probe.naturalSize;
        Q_EMIT naturalSizeChanged(naturalSize());
    }
    if (probe.animated && m_item.type.kind == ContentKind::Image) {
        startAnimation();
        return;
    }
    QSize target = targetFor(m_natural);
    if (probe.format == "svg") {
        // Vector: render for the device pixel ratio at the fitted size.
        target = (QSizeF(m_natural).scaled(QSizeF(m_decodeTarget), Qt::KeepAspectRatio) * 1.0).toSize();
    }
    m_decodeId = m_loader->decode(m_item.path, m_item.type.kind, target, m_page, m_token);
}

void ImageView::startAnimation()
{
    m_animation->start(m_item.path);
}

void ImageView::onDecodeFinished(quint64 id, const DecodedImage &image)
{
    for (int i = 0; i < m_prefetch.size(); ++i) {
        if (m_prefetch.at(i).decodeId == id) {
            const Prefetch p = m_prefetch.takeAt(i);
            if (image.ok) {
                m_cache.prepend(CacheEntry{p.item.path, mtimeOf(p.item.path), p.target, p.probe, image});
                // Do not keep full-resolution copies of neighbours.
                m_cache.first().image.fullImage = QImage();
                while (m_cache.size() > CacheSize) {
                    m_cache.removeLast();
                }
            }
            return;
        }
    }
    if (id == m_decodeId) {
        if (!image.ok) {
            fail(image.error.isEmpty() ? tr_("The image cannot be decoded") : image.error);
            return;
        }
        // Remember the current image too, for quick back-and-forth.
        if (m_page == 0 && m_probe.format != "svg") {
            CacheEntry e{m_item.path, mtimeOf(m_item.path), m_decodeTarget, m_probe, image};
            e.image.fullImage = QImage();
            m_cache.prepend(e);
            while (m_cache.size() > CacheSize) {
                m_cache.removeLast();
            }
        }
        applyDecoded(image);
        return;
    }
    if (id == m_betterId) {
        m_betterPending = false;
        if (image.ok) {
            if (m_probe.format == "svg") {
                m_display = image.image;
                m_svgRendered = image.image.size();
            } else {
                m_full = image.image;
            }
            update();
        }
    }
}

void ImageView::applyDecoded(const DecodedImage &image)
{
    m_display = image.image;
    m_displayIsFull = image.isFullResolution;
    m_full = image.isFullResolution ? QImage() : image.fullImage;
    if (m_probe.format == "svg") {
        m_svgRendered = image.image.size();
    }
    if (!m_natural.isValid()) {
        m_natural = image.naturalSize.isValid() ? image.naturalSize : image.image.size();
        Q_EMIT naturalSizeChanged(naturalSize());
    }
    m_loading = false;
    m_spinnerTimer.stop();
    fitToWindow();
    update();
    if (!m_ready) {
        m_ready = true;
        Q_EMIT ready();
    }
}

void ImageView::fail(const QString &message)
{
    m_loading = false;
    m_spinnerTimer.stop();
    Q_EMIT failed(message);
}

QSize ImageView::naturalSize() const
{
    if (!m_natural.isValid()) {
        return QSize();
    }
    return rotatedUnits().toSize();
}

QSizeF ImageView::baseUnits() const
{
    if (!m_natural.isValid()) {
        return QSizeF();
    }
    if (m_probe.format == "svg") {
        return QSizeF(m_natural); // already logical
    }
    // 100% means one image pixel per device pixel.
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    return QSizeF(m_natural) / dpr;
}

QSizeF ImageView::rotatedUnits() const
{
    const QSizeF b = baseUnits();
    return (m_rotation % 180) ? b.transposed() : b;
}

qreal ImageView::minScale() const
{
    const QSizeF u = rotatedUnits();
    const qreal fit = Geometry::fitScale(u, QSizeF(size()));
    qreal s = std::min(fit, 1.0) * 0.25;
    if (!u.isEmpty()) {
        s = std::max(s, 16.0 / std::max(u.width(), u.height()));
    }
    return std::min(s, fit);
}

qreal ImageView::maxScale() const
{
    return std::max(AbsoluteMaxScale, Geometry::fitScale(rotatedUnits(), QSizeF(size()), std::numeric_limits<qreal>::max()));
}

void ImageView::setZoom(const Geometry::ZoomState &state, bool fit)
{
    m_zoom = Geometry::clampPan(state, rotatedUnits(), QSizeF(size()));
    m_fit = fit;
    updateCursor();
    update();
    if (!fit) {
        m_betterTimer.start();
    }
}

void ImageView::zoomAt(qreal newScale, const QPointF &anchor)
{
    if (!hasImage()) {
        return;
    }
    const QSizeF units = rotatedUnits();
    const QSizeF viewport(size());
    const qreal fit = Geometry::fitScale(units, viewport);
    newScale = std::clamp(newScale, minScale(), maxScale());
    // Snap back into "fit" mode when the user zooms out to the fitted size.
    if (std::abs(newScale - fit) / fit < 0.02) {
        setZoom(Geometry::centered(units, viewport, fit), true);
        return;
    }
    setZoom(Geometry::zoomAt(m_zoom, units, viewport, newScale, anchor), false);
}

void ImageView::zoomByFactor(qreal factor, const QPointF &anchor)
{
    zoomAt(m_zoom.scale * factor, anchor);
}

void ImageView::fitToWindow()
{
    const QSizeF units = rotatedUnits();
    setZoom(Geometry::centered(units, QSizeF(size()), Geometry::fitScale(units, QSizeF(size()), m_fitUpscale ? std::numeric_limits<qreal>::max() : 1.0)), true);
}

void ImageView::prepareWindowResize()
{
    m_fitUpscale = true;
    fitToWindow();
}
void ImageView::resetPresentation()
{
    m_rotation=0; m_mirror=false; m_fitUpscale=false;
    fitToWindow();
}

void ImageView::rotateLeft()
{
    m_rotation = (m_rotation + 270) % 360;
    Q_EMIT naturalSizeChanged(naturalSize());
    fitToWindow();
}

void ImageView::toggleMirror()
{
    m_mirror = !m_mirror;
    update();
}

void ImageView::setPage(int page)
{
    if (m_probe.pageCount <= 1 || page < 0 || page >= m_probe.pageCount || page == m_page) {
        return;
    }
    m_page = page;
    if (m_token) {
        m_token->store(true);
    }
    m_token = makeCancelToken();
    m_full = QImage();
    m_betterPending = false;
    m_loading = true;
    m_loadClock.start();
    m_spinnerTimer.start();
    m_decodeId = m_loader->decode(m_item.path, m_item.type.kind, targetFor(m_natural), m_page, m_token);
    update();
}

void ImageView::requestBetterResolution()
{
    if (!hasImage() || m_betterPending || m_animation->isActive()) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const QSizeF neededDevice = baseUnits() * m_zoom.scale * dpr;
    if (m_probe.format == "svg") {
        QSize want = neededDevice.toSize().boundedTo(QSize(8192, 8192));
        if (std::abs(want.width() - m_svgRendered.width()) > m_svgRendered.width() / 8 + 2) {
            m_betterPending = true;
            m_betterId = m_loader->decode(m_item.path, m_item.type.kind, want, 0, m_token);
        }
        return;
    }
    if (m_displayIsFull || !m_full.isNull()) {
        return;
    }
    if (neededDevice.width() <= m_display.width() * 1.05) {
        return;
    }
    m_betterPending = true;
    m_betterId = m_loader->decode(m_item.path, m_item.type.kind, QSize(), m_page, m_token);
}

const QImage &ImageView::paintSource() const
{
    if (m_animation->isActive()) {
        m_frame = m_animation->currentFrame();
        return m_frame;
    }
    if (!m_full.isNull()) {
        const qreal needed = baseUnits().width() * m_zoom.scale * devicePixelRatioF();
        if (needed > m_display.width() * 1.05) {
            return m_full;
        }
    }
    return m_display;
}

bool ImageView::hasImage() const
{
    return !m_display.isNull() || (m_animation->isActive() && !m_animation->currentFrame().isNull());
}

bool ImageView::isAnimated() const
{
    return m_animation->isActive();
}

bool ImageView::isFitMode() const
{
    return m_fit;
}

QRectF ImageView::displayRect() const { return QRectF(m_zoom.offset,rotatedUnits()*m_zoom.scale); }

qreal ImageView::zoomScale() const
{
    return m_zoom.scale;
}

QPointF ImageView::zoomOffset() const
{
    return m_zoom.offset;
}

int ImageView::rotation() const
{
    return m_rotation;
}

bool ImageView::isMirrored() const
{
    return m_mirror;
}

int ImageView::currentPage() const
{
    return m_page;
}

int ImageView::pageCount() const
{
    return m_probe.pageCount;
}

QSize ImageView::sourceImageSize() const
{
    return paintSource().size();
}

bool ImageView::fullResolutionLoaded() const
{
    return m_displayIsFull || !m_full.isNull();
}

int ImageView::animationFrameIndex() const
{
    return m_animation->frameIndex();
}

ImageLoader *ImageView::loader() const
{
    return m_loader;
}

void ImageView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setClipPath(Style::windowShapeFor(this));
    p.fillRect(rect(), Qt::black);
    if (!hasImage()) {
        if (m_loading && m_loadClock.elapsed() >= SpinnerDelayMs) {
            QColor c(Qt::white);
            c.setAlphaF(0.6);
            Style::paintSpinner(p, QRectF(rect()).center(), 14, m_spinnerPhase, c);
        }
        return;
    }
    const QImage &src = paintSource();
    if (src.isNull()) {
        return;
    }
    const QSizeF units = rotatedUnits();
    const QSizeF drawSize = baseUnits() * m_zoom.scale;
    const QPointF center = m_zoom.offset + QPointF(units.width() * m_zoom.scale / 2.0, units.height() * m_zoom.scale / 2.0);

    // Smooth when scaling down or moderately up; crisp pixels when the user
    // zooms in a lot to inspect details.
    const qreal devicePerSource = drawSize.width() * devicePixelRatioF() / src.width();
    p.setRenderHint(QPainter::SmoothPixmapTransform, devicePerSource < 3.0);
    p.translate(center);
    if (m_mirror) {
        p.scale(-1, 1);
    }
    p.rotate(m_rotation);
    p.drawImage(QRectF(QPointF(-drawSize.width() / 2.0, -drawSize.height() / 2.0), drawSize), src);
    p.resetTransform();

    if (m_probe.pageCount > 1) {
        const QString text = tr_("Page %1 of %2").arg(m_page + 1).arg(m_probe.pageCount);
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        p.setFont(f);
        const QFontMetrics fm(f);
        QRectF pill(0, 0, fm.horizontalAdvance(text) + 20, fm.height() + 8);
        pill.moveCenter(QPointF(width() / 2.0, Style::OverlayMargin + pill.height() / 2.0));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        p.setPen(Qt::white);
        p.drawText(pill, Qt::AlignCenter, text);
    }
    if (m_loading && m_loadClock.elapsed() >= SpinnerDelayMs) {
        Style::paintSpinner(p, QRectF(rect()).center(), 14, m_spinnerPhase, QColor(255, 255, 255, 200));
    }
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    ContentView::resizeEvent(event);
    layoutButtons();
    if (!hasImage() || m_wheelTransaction) {
        return;
    }
    if (m_fit) {
        fitToWindow();
    } else {
        setZoom(m_zoom, false);
    }
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    if (!hasImage()) {
        event->ignore();
        return;
    }
    QPoint delta = event->angleDelta();
    const QPoint pixels = event->pixelDelta();
    // Horizontal touchpad swipes pan a zoomed image.
    if (std::abs(delta.x()) > std::abs(delta.y()) && Geometry::canPan(m_zoom, rotatedUnits(), QSizeF(size()))) {
        Geometry::ZoomState s = m_zoom;
        s.offset.rx() += pixels.isNull() ? delta.x() / 2.0 : pixels.x();
        setZoom(s, m_fit);
        event->accept();
        return;
    }
    const qreal steps = delta.y() ? delta.y() / 120.0 : pixels.y() / 60.0;
    if (steps == 0) {
        event->ignore();
        return;
    }
    // One notch ≈ 19 %; smooth for high-resolution wheels and touchpads.
    const qreal factor = std::pow(2.0, steps / 4.0);
    if (m_wheelResizesWindow && !window()->isFullScreen()) {
        // One image state, in viewport coordinates. The host may grow/shrink
        // the viewport, but cannot replace the requested scale with a fit scale.
        const auto before = m_zoom;
        const qreal scale = std::clamp(before.scale * factor, minScale(), maxScale());
        m_wheelTransaction = true;
        Q_EMIT wheelDisplaySizeRequested(rotatedUnits() * scale);
        m_wheelTransaction = false;
        setZoom(Geometry::zoomAt(before, rotatedUnits(), QSizeF(size()), scale, event->position()), false);
    } else zoomByFactor(factor, event->position());
    event->accept();
}

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_pressPos = event->position();
    m_dragged = false;
    if (!(event->modifiers() & Qt::AltModifier) && hasImage() && Geometry::canPan(m_zoom, rotatedUnits(), QSizeF(size()))) {
        m_panning = true;
        m_panStartOffset = m_zoom.offset;
        setCursor(Qt::ClosedHandCursor);
    } else {
        m_dragCandidate = true;
    }
    event->accept();
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && (event->position() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) m_dragged = true;
    if (m_panning) {
        Geometry::ZoomState s = m_zoom;
        s.offset = m_panStartOffset + (event->position() - m_pressPos);
        m_zoom = Geometry::clampPan(s, rotatedUnits(), QSizeF(size()));
        update();
        event->accept();
        return;
    }
    if (m_dragCandidate && (event->buttons() & Qt::LeftButton)) {
        if ((event->position() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragCandidate = false;
            requestWindowMove();
        }
        event->accept();
        return;
    }
    setButtonsVisible(hasImage());
    event->ignore();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const bool clicked = (m_panning || m_dragCandidate) && !m_dragged
            && (event->position() - m_pressPos).manhattanLength() < QApplication::startDragDistance();
        m_panning = false;
        m_dragCandidate = false;
        if (clicked && hasImage()) {
            m_clickPosition = event->position();
            m_clickTimer.start(QApplication::doubleClickInterval());
        }
        updateCursor();
        event->accept();
        return;
    }
    event->ignore();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !hasImage()) {
        event->ignore();
        return;
    }
    m_clickTimer.stop();
    m_dragged = true;
    // Toggle between "fit" and 100 % at the cursor.
    if (m_fit && std::abs(m_zoom.scale - 1.0) > 0.01) {
        zoomAt(1.0, event->position());
    } else {
        fitToWindow();
    }
    event->accept();
}

void ImageView::enterEvent(QEnterEvent *event)
{
    setButtonsVisible(hasImage());
    ContentView::enterEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    setButtonsVisible(false);
    ContentView::leaveEvent(event);
}

bool ImageView::handleKey(QKeyEvent *event)
{
    const QPointF center = QRectF(rect()).center();
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomByFactor(1.25, center);
        return true;
    case Qt::Key_Minus:
        zoomByFactor(0.8, center);
        return true;
    case Qt::Key_F:
    case Qt::Key_0:
        fitToWindow();
        return true;
    case Qt::Key_1:
        zoomAt(1.0, center);
        return true;
    case Qt::Key_PageDown:
        if (m_probe.pageCount > 1) {
            setPage(m_page + 1);
            return true;
        }
        return false;
    case Qt::Key_PageUp:
        if (m_probe.pageCount > 1) {
            setPage(m_page - 1);
            return true;
        }
        return false;
    case Qt::Key_R:
        if (event->modifiers() == Qt::NoModifier) {
            rotateLeft();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void ImageView::updateCursor()
{
    if (m_panning) {
        setCursor(Qt::ClosedHandCursor);
    } else if (hasImage() && Geometry::canPan(m_zoom, rotatedUnits(), QSizeF(size()))) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
}

void ImageView::layoutButtons()
{
    const QSize s = m_rotateButton->sizeHint();
    const int y = height() - s.height() - Style::OverlayMargin;
    m_flipButton->setGeometry(width() - s.width() - Style::OverlayMargin, y, s.width(), s.height());
    m_rotateButton->setGeometry(m_flipButton->x() - s.width() - 4, y, s.width(), s.height());
}

void ImageView::setButtonsVisible(bool visible)
{
    m_rotateButton->setVisible(visible);
    m_flipButton->setVisible(visible);
}

} // namespace KDEGlance
