// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QString>

class QPainter;
class QWidget;

// Visual constants and vector glyphs. The glyphs are drawn in code so the
// look does not depend on the installed icon theme and stays crisp at any
// scale factor.
namespace KDEGlance::Style
{
inline constexpr qreal CornerRadius = 12.0;
inline constexpr int ResizeBorder = 7; // grab width of the window edges
inline constexpr int ResizeCorner = 18; // extent of the corner grab zones
inline constexpr int OverlayMargin = 10;

enum class Glyph {
    Play,
    Pause,
    VolumeHigh,
    VolumeLow,
    VolumeMuted,
    Replay, // loop toggle
    Fullscreen,
    FullscreenExit,
    RotateLeft,
    Flip,
    Warning,
};

void paintGlyph(QPainter &painter, Glyph glyph, const QRectF &rect, const QColor &color);

// Rounded window outline in widget coordinates of `w` (which may be a child
// covering only part of the window).
QPainterPath windowShapeFor(const QWidget *w);
QPainterPath roundedRect(const QRectF &rect, qreal radius);

// "0:07", "12:34", "1:02:03"
QString formatTime(qint64 ms);

// Colour helpers following the active (KDE) palette.
QColor windowBackground(const QWidget *w);
QColor textColor(const QWidget *w);
bool isDark(const QWidget *w);

// The gradient used behind media controls (transparent → dark).
void paintControlsScrim(QPainter &painter, const QRectF &rect);

// Placeholder artwork for audio without embedded cover.
void paintAudioPlaceholder(QPainter &painter, const QRectF &rect, bool dark);

// Small busy spinner; `phase` in [0,1).
void paintSpinner(QPainter &painter, const QPointF &center, qreal radius, qreal phase, const QColor &color);
}
