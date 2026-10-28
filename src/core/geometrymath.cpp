// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "geometrymath.h"

#include <algorithm>
#include <cmath>

namespace KDEGlance::Geometry
{

QSize autoWindowSize(const QSize &contentSize, const QSize &workArea, double fraction, const QSize &minimum)
{
    const QSize area = workArea.isValid() && !workArea.isEmpty() ? workArea : QSize(1280, 800);
    const QSize limit(std::max(minimum.width(), int(area.width() * fraction)), std::max(minimum.height(), int(area.height() * fraction)));
    if (!contentSize.isValid() || contentSize.isEmpty()) {
        return QSize(std::min(limit.width(), 640), std::min(limit.height(), 480)).expandedTo(minimum);
    }
    QSize size = contentSize;
    if (size.width() > limit.width() || size.height() > limit.height()) {
        size = contentSize.scaled(limit, Qt::KeepAspectRatio);
    }
    size = size.expandedTo(minimum);
    // The minimum could push a very wide/tall result past the work area.
    return size.boundedTo(area.expandedTo(minimum));
}

QSize clampToArea(const QSize &size, const QSize &workArea, const QSize &minimum)
{
    if (!workArea.isValid() || workArea.isEmpty()) {
        return size.expandedTo(minimum);
    }
    return size.boundedTo(workArea).expandedTo(minimum.boundedTo(workArea));
}

QSize aspectLockedSize(const QSize &startSize, const QSize &proposed, Qt::Edges edges, const QSize &minimum, const QSize &maximum)
{
    if (startSize.isEmpty() || proposed.isEmpty()) {
        return proposed;
    }
    const double ratio = double(startSize.width()) / double(startSize.height());
    const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);

    double w = proposed.width();
    double h = proposed.height();
    if (horizontal && !vertical) {
        h = w / ratio;
    } else if (vertical && !horizontal) {
        w = h * ratio;
    } else {
        const double sw = w / startSize.width();
        const double sh = h / startSize.height();
        const double s = std::abs(sw - 1.0) >= std::abs(sh - 1.0) ? sw : sh;
        w = startSize.width() * s;
        h = startSize.height() * s;
    }

    // Respect limits without breaking the ratio.
    double lo = std::max(minimum.width() / w, minimum.height() / h);
    if (lo > 1.0) {
        w *= lo;
        h *= lo;
    }
    if (maximum.isValid() && !maximum.isEmpty()) {
        const double hi = std::min(maximum.width() / w, maximum.height() / h);
        if (hi < 1.0) {
            w *= hi;
            h *= hi;
        }
    }
    return QSize(int(std::lround(w)), int(std::lround(h)));
}

Qt::Edges edgesAt(const QPointF &pos, const QSize &windowSize, qreal border, qreal corner)
{
    Qt::Edges edges;
    const qreal w = windowSize.width();
    const qreal h = windowSize.height();
    if (pos.x() < 0 || pos.y() < 0 || pos.x() >= w || pos.y() >= h) {
        return edges;
    }
    const bool nearLeft = pos.x() < border;
    const bool nearRight = pos.x() >= w - border;
    const bool nearTop = pos.y() < border;
    const bool nearBottom = pos.y() >= h - border;
    // Corner zones extend along the edges so diagonal resizing is easy to hit.
    const bool cornerLeft = pos.x() < corner;
    const bool cornerRight = pos.x() >= w - corner;
    const bool cornerTop = pos.y() < corner;
    const bool cornerBottom = pos.y() >= h - corner;

    if (nearLeft || (cornerLeft && (nearTop || nearBottom))) {
        edges |= Qt::LeftEdge;
    }
    if (nearRight || (cornerRight && (nearTop || nearBottom))) {
        edges |= Qt::RightEdge;
    }
    if (nearTop || (cornerTop && (nearLeft || nearRight))) {
        edges |= Qt::TopEdge;
    }
    if (nearBottom || (cornerBottom && (nearLeft || nearRight))) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

QRect centeredIn(const QSize &size, const QRect &area)
{
    const QSize s = size.boundedTo(area.size());
    QRect r(QPoint(0, 0), s);
    r.moveCenter(area.center());
    r.moveLeft(std::clamp(r.left(), area.left(), area.right() - s.width() + 1));
    r.moveTop(std::clamp(r.top(), area.top(), area.bottom() - s.height() + 1));
    return r;
}

QSizeF fitInside(const QSizeF &content, const QSizeF &bounds)
{
    if (content.isEmpty() || bounds.isEmpty()) {
        return QSizeF();
    }
    return content.scaled(bounds, Qt::KeepAspectRatio);
}

qreal fitScale(const QSizeF &imageSize, const QSizeF &viewport, qreal maxFitScale)
{
    if (imageSize.isEmpty() || viewport.isEmpty()) {
        return 1.0;
    }
    const qreal s = std::min(viewport.width() / imageSize.width(), viewport.height() / imageSize.height());
    return std::min(s, maxFitScale);
}

ZoomState centered(const QSizeF &imageSize, const QSizeF &viewport, qreal scale)
{
    const QSizeF scaled = imageSize * scale;
    return ZoomState{scale, QPointF((viewport.width() - scaled.width()) / 2.0, (viewport.height() - scaled.height()) / 2.0)};
}

ZoomState clampPan(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport)
{
    ZoomState s = state;
    const QSizeF scaled = imageSize * s.scale;
    auto clampAxis = [](qreal offset, qreal content, qreal view) {
        if (content <= view) {
            return (view - content) / 2.0;
        }
        return std::clamp(offset, view - content, 0.0);
    };
    s.offset.setX(clampAxis(s.offset.x(), scaled.width(), viewport.width()));
    s.offset.setY(clampAxis(s.offset.y(), scaled.height(), viewport.height()));
    return s;
}

ZoomState zoomAt(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport, qreal newScale, const QPointF &anchor)
{
    if (state.scale <= 0) {
        return clampPan(centered(imageSize, viewport, newScale), imageSize, viewport);
    }
    // Image coordinate under the anchor stays under the anchor.
    const QPointF imagePoint = (anchor - state.offset) / state.scale;
    ZoomState s{newScale, anchor - imagePoint * newScale};
    return clampPan(s, imageSize, viewport);
}

bool canPan(const ZoomState &state, const QSizeF &imageSize, const QSizeF &viewport)
{
    const QSizeF scaled = imageSize * state.scale;
    return scaled.width() > viewport.width() + 0.5 || scaled.height() > viewport.height() + 0.5;
}

} // namespace KDEGlance::Geometry
