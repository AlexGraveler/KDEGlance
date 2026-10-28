// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "contentview.h"
#include "geometrymath.h"
#include "imagedecoder.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>

namespace KDEGlance
{
class AnimationPlayer;
class ImageLoader;

/*
 * Still images, camera RAW, SVG and animated images with zoom (mouse wheel,
 * anchored at the cursor), panning (drag when the image is larger than the
 * view), view-only rotation/flip and multi-page navigation (PageUp/Down).
 *
 * Decoding is asynchronous: a display-sized version comes first (fast
 * scaled JPEG decode, RAW embedded preview), the full resolution is decoded
 * only when the user zooms beyond it. Stale work is cancelled when another
 * file is opened, and neighbours are decoded ahead for instant switching.
 */
class ImageView : public ContentView
{
    Q_OBJECT
public:
    explicit ImageView(QWidget *parent = nullptr);
    ~ImageView() override;

    void load(const PreviewItem &item) override;
    void unload() override;
    SizeCategory sizeCategory() const override
    {
        return SizeCategory::Image;
    }
    QSize naturalSize() const override;
    bool handleKey(QKeyEvent *event) override;
    void prefetch(const QList<PreviewItem> &items) override;

    void setWheelResizesWindow(bool enabled) { m_wheelResizesWindow = enabled; }
    void setFitUpscale(bool enabled) { m_fitUpscale = enabled; }
    void prepareWindowResize();
    void resetPresentation() override;
    void setRawPreferEmbedded(bool prefer);
    // Device-pixel size images are decoded at for display (upper bound of
    // the window size); set by the window.
    void setDecodeTarget(const QSize &devicePixels);

    // --- state for tests -----------------------------------------------------
    bool hasImage() const;
    bool isAnimated() const;
    bool isFitMode() const;
    qreal zoomScale() const;
    QRectF displayRect() const;
    QPointF zoomOffset() const;
    int rotation() const;
    bool isMirrored() const;
    int currentPage() const;
    int pageCount() const;
    QSize sourceImageSize() const; // pixels of the image currently painted
    bool fullResolutionLoaded() const;
    int animationFrameIndex() const;
    ImageLoader *loader() const;

    void zoomAt(qreal newScale, const QPointF &anchor);
    void zoomByFactor(qreal factor, const QPointF &anchor);
    void fitToWindow();
    void rotateLeft();
    void toggleMirror();
    void setPage(int page);

Q_SIGNALS:
    void wheelDisplaySizeRequested(const QSizeF &size);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct CacheEntry {
        QString path;
        qint64 mtime = 0;
        QSize target;
        ImageProbe probe;
        DecodedImage image;
    };

    void onProbeFinished(quint64 id, const KDEGlance::ImageProbe &probe);
    void onDecodeFinished(quint64 id, const KDEGlance::DecodedImage &image);
    void applyDecoded(const DecodedImage &image);
    void fail(const QString &message);
    void requestBetterResolution();
    void startAnimation();

    QSizeF baseUnits() const; // logical natural size, unrotated
    QSizeF rotatedUnits() const; // logical natural size after view rotation
    qreal minScale() const;
    qreal maxScale() const;
    void setZoom(const Geometry::ZoomState &state, bool fit);
    void updateCursor();
    void layoutButtons();
    void setButtonsVisible(bool visible);
    const QImage &paintSource() const;
    QSize targetFor(const QSize &natural) const;
    qint64 mtimeOf(const QString &path) const;

    ImageLoader *m_loader;
    AnimationPlayer *m_animation;
    PreviewItem m_item;
    CancelToken m_token;
    quint64 m_probeId = 0;
    quint64 m_decodeId = 0;
    quint64 m_betterId = 0;
    ImageProbe m_probe;
    QSize m_natural; // image pixels as displayed (EXIF orientation applied)
    QImage m_display;
    QImage m_full;
    mutable QImage m_frame; // current animation frame while painting
    bool m_displayIsFull = false;
    bool m_betterPending = false;
    QSize m_svgRendered;
    bool m_loading = false;
    bool m_ready = false;
    int m_page = 0;

    Geometry::ZoomState m_zoom;
    bool m_fit = true;
    bool m_fitUpscale = false;
    bool m_wheelResizesWindow = false;
    bool m_wheelTransaction = false;
    int m_rotation = 0; // degrees clockwise: 0, 90, 180, 270
    bool m_mirror = false;

    bool m_dragged = false;
    QTimer m_clickTimer;
    QPointF m_clickPosition;
    bool m_panning = false;
    bool m_dragCandidate = false;
    QPointF m_pressPos;
    QPointF m_panStartOffset;

    bool m_rawPreferEmbedded = true;
    QSize m_decodeTarget{1920, 1080};

    QElapsedTimer m_loadClock;
    QTimer m_spinnerTimer;
    QTimer m_betterTimer;
    qreal m_spinnerPhase = 0;

    OverlayButton *m_rotateButton;
    OverlayButton *m_flipButton;

    QList<CacheEntry> m_cache; // most recent first
    struct Prefetch {
        PreviewItem item;
        CancelToken token;
        quint64 probeId = 0;
        quint64 decodeId = 0;
        ImageProbe probe;
        QSize target;
    };
    QList<Prefetch> m_prefetch;
};

} // namespace KDEGlance
