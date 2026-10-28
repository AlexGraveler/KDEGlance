// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QWidget>

class QLabel;

namespace KDEGlance
{
class OverlayButton;

// Thin seek/volume slider drawn for use on top of media (white on dark).
class MediaSlider : public QWidget
{
    Q_OBJECT
public:
    explicit MediaSlider(QWidget *parent = nullptr);

    void setRange(qint64 maximum);
    void setValue(qint64 value); // ignored while the user drags
    qint64 value() const;
    qint64 maximum() const;
    bool isDragging() const;
    // Wheel steps change the value by this amount (0 = wheel ignored).
    void setWheelStep(qint64 step);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void valueRequested(qint64 value); // while dragging and on click
    void dragFinished(qint64 value);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    qint64 valueAt(qreal x) const;
    QRectF trackRect() const;

    qint64 m_max = 0;
    qint64 m_value = 0;
    qint64 m_wheelStep = 0;
    bool m_dragging = false;
    bool m_hover = false;
};

/*
 * Playback controls: [play/pause] time [seek] duration [loop] [fullscreen] [volume].
 * Video mode shows loop and fullscreen; audio mode shows title and artist
 * above the row. The bar only reports user intent; MediaView applies it.
 */
class MediaControls : public QWidget
{
    Q_OBJECT
public:
    enum class Mode {
        Video,
        Audio,
    };
    explicit MediaControls(QWidget *parent = nullptr);

    void setMode(Mode mode);
    void setPlaying(bool playing);
    void setDuration(qint64 ms);
    void setPosition(qint64 ms);
    void setVolume(double volume, bool muted);
    void setLoop(bool loop);
    void setFullscreen(bool fullscreen);
    void setSeekable(bool seekable);
    void setTrackInfo(const QString &title, const QString &artist);

    int preferredHeight() const;

    // Widgets exposed for tests.
    OverlayButton *playButton() const;
    MediaSlider *seekSlider() const;
    MediaSlider *volumeSlider() const;
    OverlayButton *volumeButton() const;
    OverlayButton *rotateButton() const;

Q_SIGNALS:
    void playPauseClicked();
    void seekRequested(qint64 ms);
    void volumeRequested(double volume);
    void muteToggled();
    void loopToggled(bool loop);
    void fullscreenToggled();
    void rotateClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void relayout();

    Mode m_mode = Mode::Video;
    OverlayButton *m_play;
    OverlayButton *m_loop;
    OverlayButton *m_fullscreen;
    OverlayButton *m_rotate;
    OverlayButton *m_volumeButton;
    MediaSlider *m_seek;
    MediaSlider *m_volume;
    QString m_title;
    QString m_artist;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    double m_volumeValue = 1.0;
    bool m_muted = false;
};

} // namespace KDEGlance
