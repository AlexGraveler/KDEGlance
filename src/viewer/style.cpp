// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "style.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QRadialGradient>
#include <QWidget>

#include <cmath>

namespace KDEGlance::Style
{

namespace
{
// Glyphs are designed on a 24x24 grid.
QTransform gridTransform(const QRectF &rect)
{
    const qreal s = std::min(rect.width(), rect.height()) / 24.0;
    QTransform t;
    t.translate(rect.center().x() - 12 * s, rect.center().y() - 12 * s);
    t.scale(s, s);
    return t;
}

QPainterPath speakerPath()
{
    QPainterPath p;
    p.moveTo(3, 9);
    p.lineTo(7, 9);
    p.lineTo(12, 4.5);
    p.lineTo(12, 19.5);
    p.lineTo(7, 15);
    p.lineTo(3, 15);
    p.closeSubpath();
    return p;
}
} // namespace

void paintGlyph(QPainter &painter, Glyph glyph, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setTransform(gridTransform(rect), true);
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    switch (glyph) {
    case Glyph::Play: {
        QPainterPath p;
        p.moveTo(7, 4.5);
        p.lineTo(19.5, 12);
        p.lineTo(7, 19.5);
        p.closeSubpath();
        painter.drawPath(p);
        break;
    }
    case Glyph::Pause:
        painter.drawRoundedRect(QRectF(6, 4.5, 4, 15), 1, 1);
        painter.drawRoundedRect(QRectF(14, 4.5, 4, 15), 1, 1);
        break;
    case Glyph::VolumeHigh:
    case Glyph::VolumeLow:
    case Glyph::VolumeMuted: {
        painter.drawPath(speakerPath());
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        if (glyph == Glyph::VolumeMuted) {
            painter.drawLine(QPointF(15.5, 9), QPointF(21, 15));
            painter.drawLine(QPointF(21, 9), QPointF(15.5, 15));
        } else {
            painter.drawArc(QRectF(10, 8, 7, 8), -55 * 16, 110 * 16);
            if (glyph == Glyph::VolumeHigh) {
                painter.drawArc(QRectF(9, 4.5, 12, 15), -55 * 16, 110 * 16);
            }
        }
        break;
    }
    case Glyph::Replay: {
        // Repeat playback: two horizontal arrows, visually distinct from rotation.
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawLine(QPointF(5, 7), QPointF(19, 7));
        painter.drawLine(QPointF(19, 7), QPointF(15, 3));
        painter.drawLine(QPointF(19, 7), QPointF(15, 11));
        painter.drawLine(QPointF(19, 17), QPointF(5, 17));
        painter.drawLine(QPointF(5, 17), QPointF(9, 13));
        painter.drawLine(QPointF(5, 17), QPointF(9, 21));
        break;
    }
    case Glyph::Fullscreen:
    case Glyph::FullscreenExit: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        const bool exit = glyph == Glyph::FullscreenExit;
        auto corner = [&](QPointF c, qreal dx, qreal dy) {
            if (exit) {
                c += QPointF(dx * 5, dy * 5);
                dx = -dx;
                dy = -dy;
            }
            painter.drawLine(c, c + QPointF(dx * 5, 0));
            painter.drawLine(c, c + QPointF(0, dy * 5));
        };
        corner(QPointF(4, 4), 1, 1);
        corner(QPointF(20, 4), -1, 1);
        corner(QPointF(4, 20), 1, -1);
        corner(QPointF(20, 20), -1, -1);
        break;
    }
    case Glyph::RotateLeft: {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawArc(QRectF(5, 5, 14, 14), 90 * 16, -280 * 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        QPainterPath head;
        head.moveTo(12.5, 1.8);
        head.lineTo(12.5, 8.2);
        head.lineTo(7.8, 5);
        head.closeSubpath();
        painter.drawPath(head);
        break;
    }
    case Glyph::Flip: {
        QPainterPath l;
        l.moveTo(10, 5);
        l.lineTo(10, 19);
        l.lineTo(3, 19);
        l.closeSubpath();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(l);
        QPainterPath r;
        r.moveTo(14, 5);
        r.lineTo(14, 19);
        r.lineTo(21, 19);
        r.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(r);
        painter.setPen(QPen(color, 1.2, Qt::DashLine));
        painter.drawLine(QPointF(12, 2.5), QPointF(12, 21.5));
        break;
    }
    case Glyph::Warning: {
        QPainterPath t;
        t.moveTo(12, 2.5);
        t.lineTo(22.5, 21);
        t.lineTo(1.5, 21);
        t.closeSubpath();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawPath(t);
        painter.drawLine(QPointF(12, 9), QPointF(12, 14.5));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(12, 18), 1.2, 1.2);
        break;
    }
    }
    painter.restore();
}

QPainterPath roundedRect(const QRectF &rect, qreal radius)
{
    QPainterPath p;
    p.addRoundedRect(rect, radius, radius);
    return p;
}

QPainterPath windowShapeFor(const QWidget *w)
{
    const QWidget *top = w->window();
    const QPoint origin = w->mapTo(top, QPoint(0, 0));
    QRectF windowRect(QPointF(-origin.x(), -origin.y()), QSizeF(top->size()));
    const bool square = top->isFullScreen() || top->isMaximized();
    return roundedRect(windowRect, square ? 0.0 : CornerRadius);
}

QString formatTime(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}

QColor windowBackground(const QWidget *w)
{
    return w->palette().color(QPalette::Window);
}

QColor textColor(const QWidget *w)
{
    return w->palette().color(QPalette::WindowText);
}

bool isDark(const QWidget *w)
{
    return windowBackground(w).lightnessF() < 0.5;
}

void paintControlsScrim(QPainter &painter, const QRectF &rect)
{
    QLinearGradient g(rect.topLeft(), rect.bottomLeft());
    g.setColorAt(0.0, QColor(0, 0, 0, 0));
    g.setColorAt(0.55, QColor(0, 0, 0, 110));
    g.setColorAt(1.0, QColor(0, 0, 0, 175));
    painter.fillRect(rect, g);
}

void paintAudioPlaceholder(QPainter& painter, const QRectF& rect, bool dark)
{
    Q_UNUSED(dark)
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(rect.topLeft());
    const qreal width = rect.width(), height = rect.height();
    QLinearGradient bg(0, 0, width, height);
    bg.setColorAt(0, QColor("#243d4d"));
    bg.setColorAt(1, QColor("#385966"));
    painter.fillRect(QRectF(0, 0, width, height), bg);
    QRadialGradient glow(QPointF(width * .25, height * .18), std::max(width, height) * .95);
    glow.setColorAt(0, QColor(255, 255, 255, 35));
    glow.setColorAt(1, Qt::transparent);
    painter.fillRect(QRectF(0, 0, width, height), glow);
    QLinearGradient glass(0, 0, width, height);
    glass.setColorAt(0, QColor(255, 255, 255, 16));
    glass.setColorAt(.55, QColor(255, 255, 255, 2));
    glass.setColorAt(1, QColor(255, 255, 255, 16));
    painter.fillRect(QRectF(0, 0, width, height), glass);
    QRadialGradient frost(QPointF(width * .9, height * .8), width * .7);
    frost.setColorAt(0, QColor(111, 199, 201, 24));
    frost.setColorAt(1, Qt::transparent);
    painter.fillRect(QRectF(0, 0, width, height), frost);
    const qreal s = std::min(width, height) * .24;
    painter.translate(width / 2., height / 2.);
    painter.scale(s / 64., s / 64.);
    QPainterPath note;
    note.moveTo(-4, 19);
    note.lineTo(-4, -24);
    note.lineTo(26, -30);
    note.lineTo(26, 13);
    QLinearGradient ink(-20, -30, 30, 25);
    ink.setColorAt(0, QColor("#ffffff"));
    ink.setColorAt(.4, QColor("#c6dce7"));
    ink.setColorAt(1, QColor("#9bc7d6"));
    painter.setPen(QPen(QBrush(ink), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(note);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawEllipse(QPointF(-10, 20), 7, 4.8);
    painter.drawEllipse(QPointF(20, 14), 7, 4.8);
    painter.restore();
}

void paintSpinner(QPainter& painter, const QPointF& center, qreal radius, qreal phase, const QColor& color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, std::max<qreal>(2.0, radius * 0.18), Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    const QRectF r(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    painter.drawArc(r, int(-phase * 360 * 16), 270 * 16);
    painter.restore();
}

} // namespace KDEGlance::Style
