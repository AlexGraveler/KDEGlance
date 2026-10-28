// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>

// Pure geometry helpers (no widgets), shared by the viewer and its tests.
namespace KDEGlance::Geometry
{

// Smallest window the viewer will ever use.
inline constexpr QSize MinimumWindowSize{280, 200};

/*
 * Automatic window size for content of the given natural size: the content
 * is shown at 100% when it fits into fraction * workArea, otherwise it is
 * scaled down keeping its aspect ratio. Never smaller than minimum.
 */
QSize autoWindowSize(const QSize &contentSize, const QSize &workArea, double fraction, const QSize &minimum = MinimumWindowSize);

// Shrinks size to fit into the work area (never grows it).
QSize clampToArea(const QSize &size, const QSize &workArea, const QSize &minimum = MinimumWindowSize);

/*
 * Aspect-ratio-locked size for an interactive resize.
 *  startSize - window size when the resize started (defines the ratio)
 *  proposed  - size suggested by the window manager for this step
 *  edges     - the edges being dragged
 * For a single edge the dragged dimension drives the other one; for a corner
 * the dimension that changed relatively more wins. The result keeps the
 * start ratio and respects minimum and maximum (if valid).
 */
QSize aspectLockedSize(const QSize &startSize, const QSize &proposed, Qt::Edges edges, const QSize &minimum = MinimumWindowSize, const QSize &maximum = QSize());

/*
 * Which window edges a point is on. border is the grab width along the
 * edges; corner is the extent of the corner zones along each edge.
 */
Qt::Edges edgesAt(const QPointF &pos, const QSize &windowSize, qreal border, qreal corner);

// Rectangle of size `size` centered in `area`, clamped to stay inside it.
QRect centeredIn(const QSize &size, const QRect &area);

// Size of `content` scaled to fit inside `bounds` (keeps aspect; may upscale).
QSizeF fitInside(const QSizeF &content, const QSizeF &bounds);

/*
 * View state of a zoomable image inside a viewport.
 * scale  - device-independent pixels per image pixel
 * offset - position of the image's top-left corner in viewport coordinates
 */
struct ZoomState {
    qreal scale = 1.0;
    QPointF offset;
};

// Scale that fits the image into the viewport, never above `maxFitScale`.
qreal fitScale(const QSizeF &imageSize, const QSizeF &viewport, qreal maxFitScale = 1.0);

// Image centered in the viewport at the given scale.
ZoomState centered(const QSizeF &imageSize, const QSizeF &viewport, qreal scale);

/*
 * Zoom to newScale keeping the image point under `anchor` (viewport coords)
 * fixed, then clamp so the image never drifts away from the viewport.
 */
ZoomState zoomAt(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport, qreal newScale, const QPointF &anchor);

/*
 * Keeps the image covering the viewport when it is larger than the viewport
 * and centers it along an axis where it is smaller.
 */
ZoomState clampPan(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport);

// True if the image at this scale is larger than the viewport in any axis.
bool canPan(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport);

} // namespace KDEGlance::Geometry
