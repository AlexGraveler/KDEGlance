// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "mediacontrols.h"

#include "contentview.h"
#include "l10n.h"
#include "style.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace KDEGlance
{

namespace
{
constexpr int ButtonSize = 30;
constexpr int RowHeight = 40;
constexpr int Padding = 10;
constexpr int VolumeSliderWidth = 72;
} // namespace

// ---------------------------------------------------------------------------

MediaSlider::MediaSlider(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void MediaSlider::setRange(qint64 maximum)
{
    m_max = std::max<qint64>(0, maximum);
    m_value = std::clamp<qint64>(m_value, 0, m_max);
    update();
}

void MediaSlider::setValue(qint64 value)
{
    if (m_dragging) {
        return;
    }
    value = std::clamp<qint64>(value, 0, m_max);
    if (value != m_value) {
        m_value = value;
        update();
    }
}

qint64 MediaSlider::value() const
{
    return m_value;
}

qint64 MediaSlider::maximum() const
{
    return m_max;
}

bool MediaSlider::isDragging() const
{
    return m_dragging;
}

void MediaSlider::setWheelStep(qint64 step)
{
    m_wheelStep = step;
}

QSize MediaSlider::sizeHint() const
{
    return QSize(160, 20);
}

QSize MediaSlider::minimumSizeHint() const
{
    return QSize(40, 20);
}

QRectF MediaSlider::trackRect() const
{
    const qreal h = (m_hover || m_dragging) ? 5.0 : 4.0;
    return QRectF(6, height() / 2.0 - h / 2.0, std::max(0, width() - 12), h);
}

qint64 MediaSlider::valueAt(qreal x) const
{
    const QRectF t = trackRect();
    if (t.width() <= 0 || m_max <= 0) {
        return 0;
    }
    const qreal f = std::clamp((x - t.left()) / t.width(), 0.0, 1.0);
    return qint64(std::llround(f * m_max));
}

void MediaSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = trackRect();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, isEnabled() ? 80 : 40));
    p.drawRoundedRect(t, t.height() / 2, t.height() / 2);
    const qreal f = m_max > 0 ? double(m_value) / double(m_max) : 0.0;
    QRectF done = t;
    done.setWidth(t.width() * f);
    p.setBrush(QColor(255, 255, 255, isEnabled() ? 235 : 90));
    p.drawRoundedRect(done, t.height() / 2, t.height() / 2);
    if (isEnabled()) {
        const qreal r = (m_hover || m_dragging) ? 7.0 : 6.0;
        p.drawEllipse(QPointF(done.right(), t.center().y()), r, r);
    }
}

void MediaSlider::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled()) {
        event->ignore();
        return;
    }
    m_dragging = true;
    m_value = valueAt(event->position().x());
    update();
    Q_EMIT valueRequested(m_value);
    event->accept();
}

void MediaSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        event->ignore();
        return;
    }
    const qint64 v = valueAt(event->position().x());
    if (v != m_value) {
        m_value = v;
        update();
        Q_EMIT valueRequested(m_value);
    }
    event->accept();
}

void MediaSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        event->ignore();
        return;
    }
    m_dragging = false;
    update();
    Q_EMIT dragFinished(m_value);
    event->accept();
}

void MediaSlider::wheelEvent(QWheelEvent *event)
{
    if (m_wheelStep <= 0 || !isEnabled()) {
        event->ignore();
        return;
    }
    const qreal steps = event->angleDelta().y() / 120.0;
    if (steps == 0) {
        event->ignore();
        return;
    }
    const qint64 v = std::clamp<qint64>(m_value + qint64(std::lround(steps * m_wheelStep)), 0, m_max);
    m_value = v;
    update();
    Q_EMIT valueRequested(v);
    event->accept();
}

void MediaSlider::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void MediaSlider::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}

// ---------------------------------------------------------------------------

MediaControls::MediaControls(QWidget *parent)
    : QWidget(parent)
    , m_play(new OverlayButton(int(Style::Glyph::Play), this))
    , m_loop(new OverlayButton(int(Style::Glyph::Replay), this))
    , m_fullscreen(new OverlayButton(int(Style::Glyph::Fullscreen), this))
    , m_rotate(new OverlayButton(int(Style::Glyph::RotateLeft), this))
    , m_volumeButton(new OverlayButton(int(Style::Glyph::VolumeHigh), this))
    , m_seek(new MediaSlider(this))
    , m_volume(new MediaSlider(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    for (OverlayButton *b : {m_play, m_loop, m_fullscreen, m_rotate, m_volumeButton}) {
        b->setBackdrop(false);
    }
    m_rotate->setToolTip(tr_("Rotate 90° counterclockwise"));
    m_loop->setObjectName(QStringLiteral("repeatPlayback"));
    connect(m_rotate, &QAbstractButton::clicked, this, &MediaControls::rotateClicked);
    m_rotate->setObjectName(QStringLiteral("videoRotate"));
    m_play->setObjectName(QStringLiteral("playButton"));
    m_loop->setCheckable(true);
    m_play->setToolTip(tr_("Play / Pause"));
    m_loop->setToolTip(tr_("Repeat"));
    m_fullscreen->setToolTip(tr_("Full screen"));
    m_volumeButton->setToolTip(tr_("Mute"));
    m_seek->setWheelStep(1000);
    m_volume->setRange(100);
    m_volume->setWheelStep(5);
    m_volume->setToolTip(tr_("Volume"));

    connect(m_play, &QAbstractButton::clicked, this, &MediaControls::playPauseClicked);
    connect(m_loop, &QAbstractButton::toggled, this, &MediaControls::loopToggled);
    connect(m_fullscreen, &QAbstractButton::clicked, this, &MediaControls::fullscreenToggled);
    connect(m_volumeButton, &QAbstractButton::clicked, this, &MediaControls::muteToggled);
    connect(m_seek, &MediaSlider::valueRequested, this, &MediaControls::seekRequested);
    connect(m_seek, &MediaSlider::dragFinished, this, &MediaControls::seekRequested);
    connect(m_volume, &MediaSlider::valueRequested, this, [this](qint64 v) {
        Q_EMIT volumeRequested(v / 100.0);
    });
    setMode(Mode::Video);
}

void MediaControls::setMode(Mode mode)
{
    m_mode = mode;
    m_rotate->setVisible(mode == Mode::Video);
    m_loop->setVisible(mode == Mode::Video);
    m_fullscreen->setVisible(mode == Mode::Video);
    relayout();
    update();
}

int MediaControls::preferredHeight() const
{
    if (m_mode == Mode::Audio) {
        return RowHeight + Padding + fontMetrics().height() * 2 + 14;
    }
    return RowHeight + Padding + 26; // room for the scrim to fade in
}

void MediaControls::setPlaying(bool playing)
{
    m_play->setGlyph(int(playing ? Style::Glyph::Pause : Style::Glyph::Play));
}

void MediaControls::setDuration(qint64 ms)
{
    m_duration = std::max<qint64>(0, ms);
    m_seek->setRange(m_duration);
    relayout();
    update();
}

void MediaControls::setPosition(qint64 ms)
{
    m_position = ms;
    m_seek->setValue(ms);
    update(QRect(0, height() - RowHeight - Padding, width(), RowHeight + Padding));
}

void MediaControls::setVolume(double volume, bool muted)
{
    m_volumeValue = volume;
    m_muted = muted;
    m_volume->setValue(qint64(std::lround(volume * 100)));
    Style::Glyph g = Style::Glyph::VolumeHigh;
    if (muted || volume <= 0.001) {
        g = Style::Glyph::VolumeMuted;
    } else if (volume < 0.5) {
        g = Style::Glyph::VolumeLow;
    }
    m_volumeButton->setGlyph(int(g));
}

void MediaControls::setLoop(bool loop)
{
    const QSignalBlocker blocker(m_loop);
    m_loop->setChecked(loop);
    m_loop->update();
}

void MediaControls::setFullscreen(bool fullscreen)
{
    m_fullscreen->setGlyph(int(fullscreen ? Style::Glyph::FullscreenExit : Style::Glyph::Fullscreen));
}

void MediaControls::setSeekable(bool seekable)
{
    m_seek->setEnabled(seekable);
}

void MediaControls::setTrackInfo(const QString &title, const QString &artist)
{
    m_title = title;
    m_artist = artist;
    update();
}

OverlayButton *MediaControls::playButton() const
{
    return m_play;
}

MediaSlider *MediaControls::seekSlider() const
{
    return m_seek;
}

MediaSlider *MediaControls::volumeSlider() const
{
    return m_volume;
}

OverlayButton *MediaControls::volumeButton() const
{
    return m_volumeButton;
}

OverlayButton *MediaControls::rotateButton() const { return m_rotate; }

void MediaControls::relayout()
{
    const int rowTop = height() - RowHeight - Padding / 2;
    const int cy = rowTop + RowHeight / 2;
    int x = Padding;
    m_play->setGeometry(x, cy - ButtonSize / 2, ButtonSize, ButtonSize);
    x += ButtonSize + 6;

    const QFontMetrics fm = fontMetrics();
    const QString longest = Style::formatTime(std::max<qint64>(m_duration, 1000));
    const int timeWidth = fm.horizontalAdvance(longest) + 6;

    int right = width() - Padding;
    m_volumeButton->setGeometry(right - ButtonSize, cy - ButtonSize / 2, ButtonSize, ButtonSize);
    right -= ButtonSize;
    const bool roomForVolumeSlider = width() >= 330;
    m_volume->setVisible(roomForVolumeSlider);
    if (roomForVolumeSlider) {
        m_volume->setGeometry(right - VolumeSliderWidth, cy - 10, VolumeSliderWidth, 20);
        right -= VolumeSliderWidth + 4;
    }
    if (m_mode == Mode::Video) {
        right -= 2;
        m_rotate->setGeometry(right - ButtonSize, cy - ButtonSize / 2, ButtonSize, ButtonSize);
        right -= ButtonSize + 2;
        m_fullscreen->setGeometry(right - ButtonSize, cy - ButtonSize / 2, ButtonSize, ButtonSize);
        right -= ButtonSize + 2;
        m_loop->setGeometry(right - ButtonSize, cy - ButtonSize / 2, ButtonSize, ButtonSize);
        right -= ButtonSize + 4;
    }
    // [time] seek [duration]
    const int seekLeft = x + timeWidth + 6; // room for the handle at 0:00
    const int seekRight = right - timeWidth - 4;
    m_seek->setGeometry(seekLeft, cy - 10, std::max(20, seekRight - seekLeft), 20);
}

void MediaControls::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void MediaControls::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setClipPath(Style::windowShapeFor(this));
    Style::paintControlsScrim(p, rect());

    p.setPen(QColor(255, 255, 255, 230));
    QFont small = font();
    small.setPointSizeF(font().pointSizeF() * 0.85);
    small.setFeature(QFont::Tag("tnum"), 1); // tabular digits: no jitter
    p.setFont(small);
    const QFontMetrics fm(small);
    const int cy = m_seek->geometry().center().y();
    const QRect leftText(m_play->geometry().right() + 4, cy - fm.height() / 2, m_seek->x() - m_play->geometry().right() - 6, fm.height());
    p.drawText(leftText, Qt::AlignVCenter | Qt::AlignRight, Style::formatTime(m_position));
    const int afterSeek = m_seek->geometry().right() + 4;
    const int durRight = (m_mode == Mode::Video ? m_loop->x() : (m_volume->isVisible() ? m_volume->x() : m_volumeButton->x())) - 4;
    p.drawText(QRect(afterSeek, cy - fm.height() / 2, std::max(0, durRight - afterSeek), fm.height()), Qt::AlignVCenter | Qt::AlignLeft,
               Style::formatTime(m_duration));

    if (m_mode == Mode::Audio && !m_title.isEmpty()) {
        QFont bold = font();
        bold.setBold(true);
        const QFontMetrics bfm(bold);
        const int textLeft = Padding + 4;
        const int textWidth = width() - 2 * (Padding + 4);
        int y = m_seek->geometry().top() - 8 - fm.height() - bfm.height();
        if (m_artist.isEmpty()) {
            y += fm.height();
        }
        p.setFont(bold);
        p.setPen(Qt::white);
        p.drawText(QRect(textLeft, y, textWidth, bfm.height()), Qt::AlignLeft | Qt::AlignVCenter, bfm.elidedText(m_title, Qt::ElideRight, textWidth));
        if (!m_artist.isEmpty()) {
            p.setFont(small);
            p.setPen(QColor(255, 255, 255, 200));
            p.drawText(QRect(textLeft, y + bfm.height(), textWidth, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       fm.elidedText(m_artist, Qt::ElideRight, textWidth));
        }
    }
}

void MediaControls::wheelEvent(QWheelEvent *event)
{
    // The wheel over the volume button behaves like over the slider.
    if (m_volumeButton->geometry().contains(event->position().toPoint())) {
        const qreal steps = event->angleDelta().y() / 120.0;
        const double v = std::clamp(m_volumeValue + steps * 0.05, 0.0, 1.0);
        Q_EMIT volumeRequested(v);
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace KDEGlance
