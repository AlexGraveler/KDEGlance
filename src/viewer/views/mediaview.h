// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "contentview.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMediaPlayer>
#include <QPointer>
#include <QTimer>
#include <QVideoFrame>

class QAudioOutput;
class QVideoSink;
class QProcess;
class QTemporaryDir;

namespace KDEGlance
{
class MediaControls;
class Settings;

// Paints video frames letterboxed into the rounded window.
class VideoSurface : public QWidget
{
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent = nullptr);
    void setFrame(const QVideoFrame &frame);
    void clear();
    QSize frameSize() const; // oriented size of the last frame, in pixels
    bool hasFrame() const;
    qint64 framesShown() const;
    void rotateLeft();
    void resetPresentation();
    int rotation() const { return m_rotation; }
    qreal zoom() const { return m_zoom; }
    void zoomAt(qreal zoom, const QPointF &anchor);
    void panBy(const QPointF &delta);
    QRectF imageRect() const;

Q_SIGNALS:
    void frameSizeChanged(const QSize &size);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void clampPan();
    int m_rotation = 0;
    qreal m_zoom = 1.0;
    QPointF m_pan;
    QVideoFrame m_frame;
    QImage m_image;
    bool m_dirty = false;
    QSize m_size;
    qint64 m_frames = 0;
};

/*
 * Video player and compact audio mini-player sharing one QMediaPlayer
 * (FFmpeg backend: broad container/codec support, hardware decoding with
 * software fallback, PulseAudio/PipeWire output). The layout follows the
 * actual streams: a ".ogg"/".mkv"/".rm" without picture gets the audio
 * layout, a ".m4a" with video gets the video one.
 *
 * Space is never used for play/pause (it closes the preview): the play button
 * does that. Unloading stops playback and releases the decoder and the audio
 * stream immediately.
 */
class MediaView : public ContentView
{
    Q_OBJECT
public:
    explicit MediaView(Settings *settings, QWidget *parent = nullptr);
    ~MediaView() override;

    void load(const PreviewItem &item) override;
    void unload() override;
    void resetPresentation() override;
    SizeCategory sizeCategory() const override;
    QSize naturalSize() const override;
    bool handleKey(QKeyEvent *event) override;

    // Plays a remote stream (RealMedia metafile entries etc.).
    void loadStream(const QUrl &url, const QString &title);

    // --- state for tests -------------------------------------------------
    QMediaPlayer *player() const;
    MediaControls *controls() const;
    VideoSurface *surface() const;
    bool hasVideo() const;
    QString title() const;
    QString artist() const;
    bool hasCoverArt() const;
    bool isLoaded() const;

    static constexpr QSize AudioWindowSize{400, 400};

Q_SIGNALS:
    void fullscreenRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void startSource(const QUrl &source);
    void onMediaStatus(QMediaPlayer::MediaStatus status);
    void onError(QMediaPlayer::Error error, const QString &message);
    void updateMetaData();
    void setHasVideo(bool video);
    void layoutChildren();
    void applyVolume(double volume);
    void applyModuleSettings();
    void toggleMute();
    void showControls();
    void scheduleHide();
    void lookForFolderCover();
    void startMidi(const QString &path);
    QString friendlyError(QMediaPlayer::Error error, const QString &message) const;

    Settings *m_settings;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoSink *m_sink;
    VideoSurface *m_surface;
    MediaControls *m_controls;
    QTimer m_hideTimer;
    QTimer m_loadTimeout;
    QTimer m_seekThrottle;
    qint64 m_pendingSeek = -1;

    PreviewItem m_item;
    quint64 m_loadGeneration = 0;
    bool m_active = false;
    bool m_loaded = false;
    bool m_ready = false;
    bool m_hasVideo = false;
    bool m_initialKindIsVideo = false;
    QString m_title;
    QString m_artist;
    QImage m_cover;
    QSize m_videoSize;

    bool m_dragged = false;
    bool m_panning = false;
    QPointF m_lastPan;
    QTimer m_clickTimer;
    QPointF m_clickPosition;
    bool m_dragCandidate = false;
    QPointF m_pressPos;

    QPointer<QProcess> m_midiProcess;
    std::unique_ptr<QTemporaryDir> m_midiDir;
};

} // namespace KDEGlance
