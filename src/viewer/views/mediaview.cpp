// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "mediaview.h"

#include "formatregistry.h"
#include "l10n.h"
#include "mediacontrols.h"
#include "midisupport.h"
#include "settings.h"
#include "style.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVideoSink>
#include <QWheelEvent>
#include <QKeyEvent>
#include <cmath>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
constexpr int ControlsHideDelayMs = 2500;
constexpr int LoadTimeoutMs = 20000;
constexpr int FirstFrameGraceMs = 1500;
constexpr int SeekThrottleMs = 60;
} // namespace

// ---------------------------------------------------------------------------

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMouseTracking(true);
}

void VideoSurface::setFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) {
        return;
    }
    m_frame = frame;
    m_dirty = true;
    ++m_frames;
    update();
}

void VideoSurface::clear()
{
    m_rotation = 0;
    m_zoom = 1.0;
    m_pan = {};
    m_frame = QVideoFrame();
    m_image = QImage();
    m_dirty = false;
    m_size = QSize();
    m_frames = 0;
    update();
}

QSize VideoSurface::frameSize() const
{
    return m_size;
}

bool VideoSurface::hasFrame() const
{
    return !m_image.isNull();
}

qint64 VideoSurface::framesShown() const
{
    return m_frames;
}

QRectF VideoSurface::imageRect() const
{
    const QSizeF target = QSizeF(m_size).scaled(QSizeF(size()), Qt::KeepAspectRatio) * m_zoom;
    QRectF result(QPointF(), target);
    result.moveCenter(QRectF(rect()).center() + m_pan);
    return result;
}
void VideoSurface::rotateLeft()
{
    m_rotation = (m_rotation + 270) % 360;
    m_dirty = true;
    m_pan = {};
    update();
}
void VideoSurface::resetPresentation()
{
    m_rotation=0; m_zoom=1; m_pan={}; m_dirty=true; update();
}
void VideoSurface::zoomAt(qreal zoom, const QPointF &anchor)
{
    zoom = std::clamp(zoom, 0.1, 16.0);
    const QPointF center = QRectF(rect()).center();
    m_pan = anchor - center - (anchor - center - m_pan) * (zoom / m_zoom);
    m_zoom = zoom;
    clampPan();
    update();
}
void VideoSurface::clampPan()
{
    const auto draw = imageRect().size();
    const qreal x = std::max<qreal>(0, (draw.width() - width()) / 2);
    const qreal y = std::max<qreal>(0, (draw.height() - height()) / 2);
    m_pan.setX(std::clamp(m_pan.x(), -x, x));
    m_pan.setY(std::clamp(m_pan.y(), -y, y));
}
void VideoSurface::panBy(const QPointF &delta) { m_pan += delta; clampPan(); update(); }

void VideoSurface::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setClipPath(Style::windowShapeFor(this));
    p.fillRect(rect(), Qt::black);
    if (!hasFrame()) { p.setPen(Qt::white); p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Loading…")); }
    if (m_dirty) {
        // toImage() applies the stream's rotation/mirroring metadata.
        m_image = m_frame.toImage();
        if (m_rotation && !m_image.isNull()) m_image = m_image.transformed(QTransform().rotate(m_rotation));
        m_dirty = false;
        if (!m_image.isNull() && m_image.size() != m_size) {
            m_size = m_image.size();
            Q_EMIT frameSizeChanged(m_size);
        }
    }
    if (m_image.isNull()) {
        return;
    }
    // Keep the video's own aspect ratio; bars fill the rest (no distortion).
    const QRectF r = imageRect();
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_image);
}

// ---------------------------------------------------------------------------

MediaView::MediaView(Settings *settings, QWidget *parent)
    : ContentView(parent)
    , m_settings(settings)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_sink(new QVideoSink(this))
    , m_surface(new VideoSurface(this))
    , m_controls(new MediaControls(this))
{
    setObjectName(QStringLiteral("mediaView"));
    m_surface->setObjectName(QStringLiteral("videoSurface"));
    m_controls->seekSlider()->setObjectName(QStringLiteral("seekSlider"));
    m_clickTimer.setSingleShot(true);
    connect(&m_clickTimer, &QTimer::timeout, this, [this] {
        const QRectF visible = m_hasVideo ? m_surface->imageRect().intersected(QRectF(rect())) : QRectF(rect());
        contentClick(m_clickPosition, visible);
    });
    connect(m_controls, &MediaControls::rotateClicked, m_surface, &VideoSurface::rotateLeft);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_sink);
    m_surface->installEventFilter(this);
    m_controls->installEventFilter(this);
    applyVolume(m_settings->volume());
    m_audio->setMuted(m_settings->muted());
    m_controls->setVolume(m_settings->volume(), m_settings->muted());
    m_controls->setLoop(m_settings->moduleLoopVideo());

    connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (m_active && m_hasVideo) {
            m_surface->setFrame(frame);
        }
    });
    connect(m_surface, &VideoSurface::frameSizeChanged, this, [this](const QSize &size) {
        m_loadTimeout.stop();
        m_videoSize = size;
        Q_EMIT naturalSizeChanged(naturalSize());
        if (!m_ready && m_active) {
            m_ready = true;
            Q_EMIT ready();
        }
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &MediaView::onMediaStatus);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &MediaView::onError);
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this](bool video) {
        if (m_active && m_loaded) {
            setHasVideo(video);
        }
    });
    connect(m_player, &QMediaPlayer::durationChanged, m_controls, &MediaControls::setDuration);
    connect(m_player, &QMediaPlayer::positionChanged, m_controls, &MediaControls::setPosition);
    connect(m_player, &QMediaPlayer::seekableChanged, m_controls, &MediaControls::setSeekable);
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &MediaView::updateMetaData);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        m_controls->setPlaying(state == QMediaPlayer::PlayingState);
        showControls();
    });

    connect(m_controls, &MediaControls::playPauseClicked, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia) {
                m_player->setPosition(0);
            }
            m_player->play();
        }
    });
    m_seekThrottle.setSingleShot(true);
    m_seekThrottle.setInterval(SeekThrottleMs);
    connect(&m_seekThrottle, &QTimer::timeout, this, [this] {
        if (m_pendingSeek >= 0) {
            m_player->setPosition(m_pendingSeek);
            m_pendingSeek = -1;
        }
    });
    connect(m_controls, &MediaControls::seekRequested, this, [this](qint64 ms) {
        // Live scrubbing without flooding the demuxer with seeks.
        if (m_seekThrottle.isActive()) {
            m_pendingSeek = ms;
        } else {
            m_player->setPosition(ms);
            m_seekThrottle.start();
        }
    });
    connect(m_controls, &MediaControls::volumeRequested, this, [this](double v) {
        applyVolume(v);
        if (v > 0.0 && m_audio->isMuted()) {
            m_audio->setMuted(false);
            m_settings->setModuleMuted(sizeCategory(),false);
        }
        m_settings->setModuleVolume(sizeCategory(),v);
        m_controls->setVolume(v, m_audio->isMuted());
    });
    connect(m_controls, &MediaControls::muteToggled, this, &MediaView::toggleMute);
    connect(m_controls, &MediaControls::loopToggled, this, [this](bool loop) {
        m_settings->setModuleLoopVideo(loop);
    });
    connect(m_controls, &MediaControls::fullscreenToggled, this, &MediaView::fullscreenRequested);

    // Follow the system default output (e.g. headphones plugged in).
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        m_audio->setDevice(QMediaDevices::defaultAudioOutput());
    });

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(ControlsHideDelayMs);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] {
        if (m_hasVideo && m_player->playbackState() == QMediaPlayer::PlayingState && !m_controls->underMouse()
            && !m_controls->seekSlider()->isDragging()) {
            m_controls->hide();
            if (window()->isFullScreen()) {
                setCursor(Qt::BlankCursor);
            }
        }
    });
    m_loadTimeout.setSingleShot(true);
    m_loadTimeout.setInterval(LoadTimeoutMs);
    connect(&m_loadTimeout, &QTimer::timeout, this, [this] {
        if (m_active && (!m_loaded || (m_hasVideo && !m_surface->hasFrame() && m_settings->autoplay()))) {
            const QString msg = tr_("Opening the file takes too long; it may be damaged or on a slow device");
            unload();
            Q_EMIT failed(msg);
        }
    });
    m_surface->hide();
}

MediaView::~MediaView()
{
    unload();
}

QMediaPlayer *MediaView::player() const
{
    return m_player;
}

MediaControls *MediaView::controls() const
{
    return m_controls;
}

VideoSurface *MediaView::surface() const
{
    return m_surface;
}

bool MediaView::hasVideo() const
{
    return m_hasVideo;
}

QString MediaView::title() const
{
    return m_title;
}

QString MediaView::artist() const
{
    return m_artist;
}

bool MediaView::hasCoverArt() const
{
    return !m_cover.isNull();
}

bool MediaView::isLoaded() const
{
    return m_loaded;
}

SizeCategory MediaView::sizeCategory() const
{
    return m_hasVideo ? SizeCategory::Video : SizeCategory::Audio;
}

QSize MediaView::naturalSize() const
{
    if (!m_hasVideo) {
        return AudioWindowSize;
    }
    if (!m_videoSize.isValid()) {
        return QSize();
    }
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    return (QSizeF(m_videoSize) / dpr).toSize();
}

void MediaView::load(const PreviewItem &item)
{
    unload();
    m_active = true;
    m_item = item;
    m_initialKindIsVideo = item.type.kind == ContentKind::Video;
    m_title = QFileInfo(item.path).fileName();
    m_artist.clear();
    setHasVideo(m_initialKindIsVideo);
    m_controls->setTrackInfo(m_title, QString());
    m_controls->setDuration(0);
    m_controls->setPosition(0);
    m_controls->setLoop(m_settings->moduleLoopVideo());
    showControls();
    if (!m_initialKindIsVideo) {
        lookForFolderCover();
    }
    m_loadTimeout.start();
    if (item.type.kind == ContentKind::Midi) {
        startMidi(item.path);
        return;
    }
    startSource(QUrl::fromLocalFile(item.path));
}

void MediaView::loadStream(const QUrl &url, const QString &title)
{
    unload();
    m_active = true;
    m_item = PreviewItem{url, QString(), FileTypeInfo{}};
    m_item.type.kind = ContentKind::Audio;
    m_title = title.isEmpty() ? url.toDisplayString() : title;
    setHasVideo(false);
    m_controls->setTrackInfo(m_title, url.host());
    m_loadTimeout.start();
    startSource(url);
}

void MediaView::startSource(const QUrl &source)
{
    m_player->setSource(source);
    if (m_settings->autoplay()) {
        m_player->play(); // starts as soon as the media is loaded
    }
}

void MediaView::unload()
{
    m_clickTimer.stop();
    m_panning = m_dragCandidate = false;
    ++m_loadGeneration;
    m_active = false;
    m_loadTimeout.stop();
    m_hideTimer.stop();
    m_seekThrottle.stop();
    m_pendingSeek = -1;
    if (m_midiProcess) {
        m_midiProcess->disconnect(this);
        m_midiProcess->kill();
        m_midiProcess->waitForFinished(200);
        m_midiProcess->deleteLater();
    }
    // stop() + empty source release the demuxer, decoders and audio stream.
    m_player->stop();
    m_player->setSource(QUrl());
    m_midiDir.reset();
    m_surface->clear();
    m_loaded = false;
    m_ready = false;
    m_cover = QImage();
    m_videoSize = QSize();
    m_title.clear();
    m_artist.clear();
    unsetCursor();
    update();
}

void MediaView::onMediaStatus(QMediaPlayer::MediaStatus status)
{
    if (!m_active) {
        return;
    }
    switch (status) {
    case QMediaPlayer::LoadedMedia:
    case QMediaPlayer::BufferedMedia:
        if (!m_loaded) {
            m_loaded = true;
            m_loadTimeout.stop();
            updateMetaData();
            setHasVideo(m_player->hasVideo());
            if (!m_hasVideo) {
                if (!m_ready) {
                    m_ready = true;
                    Q_EMIT ready();
                }
            } else {
                if (m_settings->autoplay() && !m_surface->hasFrame()) m_loadTimeout.start();
                // Normally the first frame makes us ready; do not wait forever
                // for streams that never deliver one (e.g. paused, no autoplay).
                const auto generation = m_loadGeneration;
                QTimer::singleShot(FirstFrameGraceMs, this, [this, generation] {
                    if (generation == m_loadGeneration && m_active && !m_ready) {
                        m_ready = true;
                        Q_EMIT ready();
                    }
                });
            }
        }
        break;
    case QMediaPlayer::EndOfMedia:
        if (m_hasVideo && m_settings->moduleLoopVideo()) {
            m_player->setPosition(0);
            m_player->play();
        } else {
            m_controls->setPlaying(false);
            showControls();
        }
        break;
    default:
        break;
    }
}

QString MediaView::friendlyError(QMediaPlayer::Error error, const QString &message) const
{
    QString text;
    switch (error) {
    case QMediaPlayer::FormatError:
        text = tr_("This audio/video format or codec is not supported by the installed decoders");
        break;
    case QMediaPlayer::ResourceError:
        text = tr_("The file cannot be opened; it may be damaged");
        break;
    case QMediaPlayer::NetworkError:
        text = tr_("The stream cannot be reached");
        break;
    case QMediaPlayer::AccessDeniedError:
        text = tr_("Access to the file was denied");
        break;
    default:
        text = tr_("Playback failed");
        break;
    }
    const QString hint = FormatRegistry::missingDecoderHint(QFileInfo(m_item.path).suffix());
    if (!message.isEmpty()) {
        text += u"\n("_s + message + u")"_s;
    }
    if (error == QMediaPlayer::FormatError && !hint.isEmpty()) {
        text += u"\n"_s + hint;
    }
    return text;
}

void MediaView::onError(QMediaPlayer::Error error, const QString &message)
{
    if (!m_active || error == QMediaPlayer::NoError) {
        return;
    }
    const QString text = friendlyError(error, message);
    unload();
    Q_EMIT failed(text);
}

void MediaView::updateMetaData()
{
    if (!m_active) {
        return;
    }
    const QMediaMetaData md = m_player->metaData();
    const QString title = md.stringValue(QMediaMetaData::Title).trimmed();
    if (!title.isEmpty()) {
        m_title = title;
    }
    QStringList artists = md.value(QMediaMetaData::ContributingArtist).toStringList();
    if (artists.isEmpty()) {
        artists = md.value(QMediaMetaData::AlbumArtist).toStringList();
    }
    if (artists.isEmpty()) {
        const QString author = md.stringValue(QMediaMetaData::Author);
        if (!author.isEmpty()) {
            artists << author;
        }
    }
    artists.removeAll(QString());
    if (!artists.isEmpty()) {
        m_artist = artists.join(u", "_s);
    }
    QImage cover = md.value(QMediaMetaData::CoverArtImage).value<QImage>();
#if QT_VERSION < QT_VERSION_CHECK(6, 12, 0)
    if (cover.isNull()) {
        cover = md.value(QMediaMetaData::ThumbnailImage).value<QImage>();
    }
#endif
    if (!cover.isNull()) {
        m_cover = cover;
    }
    m_controls->setTrackInfo(m_title, m_artist);
    update();
}

void MediaView::lookForFolderCover()
{
    // Common cover files next to the track (FLAC/WAV rips without tags).
    const QDir dir = QFileInfo(m_item.path).absoluteDir();
    static const QStringList names{u"cover"_s, u"folder"_s, u"front"_s, u"album"_s, u"albumart"_s};
    static const QStringList exts{u"jpg"_s, u"jpeg"_s, u"png"_s, u"webp"_s};
    const QStringList entries = dir.entryList(QDir::Files | QDir::Readable);
    for (const QString &name : names) {
        for (const QString &entry : entries) {
            const QFileInfo fi(entry);
            if (fi.completeBaseName().compare(name, Qt::CaseInsensitive) == 0 && exts.contains(fi.suffix().toLower())) {
                QImage img(dir.absoluteFilePath(entry));
                if (!img.isNull()) {
                    // Covers can be huge; keep a reasonable copy.
                    if (img.width() > 1600 || img.height() > 1600) {
                        img = img.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                    m_cover = img;
                    return;
                }
            }
        }
    }
}

void MediaView::startMidi(const QString &path)
{
    const QString fluidsynth = MidiSupport::synthesizer();
    const QString soundfont = MidiSupport::soundfont();
    if (fluidsynth.isEmpty() || soundfont.isEmpty()) {
        const QString msg = tr_("MIDI playback needs a synthesizer: install fluidsynth and a SoundFont (e.g. soundfont-fluid)");
        const auto generation = m_loadGeneration;
        QTimer::singleShot(0, this, [this, msg, generation] {
            if (generation != m_loadGeneration) return;
            unload();
            Q_EMIT failed(msg);
        });
        return;
    }
    m_midiDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + u"/kdeglance-midi-XXXXXX"_s);
    if (!m_midiDir->isValid()) {
        unload();
        Q_EMIT failed(tr_("The MIDI file could not be rendered"));
        return;
    }
    const QString wav = m_midiDir->filePath(u"render.wav"_s);
    auto *process = new QProcess(this);
    m_midiProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process, wav](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (!m_active) {
            return;
        }
        if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(wav)) {
            unload();
            Q_EMIT failed(tr_("The MIDI file could not be rendered"));
            return;
        }
        startSource(QUrl::fromLocalFile(wav));
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_active) {
            unload();
            Q_EMIT failed(tr_("The MIDI file could not be rendered"));
        }
    });
    process->start(fluidsynth, MidiSupport::arguments(wav, soundfont, path));
}

void MediaView::setHasVideo(bool video)
{
    const bool changed = video != m_hasVideo;
    m_hasVideo = video;
    applyModuleSettings();
    m_controls->setMode(video ? MediaControls::Mode::Video : MediaControls::Mode::Audio);
    m_surface->setVisible(video);
    layoutChildren();
    showControls();
    update();
    if (changed) {
        Q_EMIT sizeCategoryChanged(sizeCategory());
        Q_EMIT naturalSizeChanged(naturalSize());
    }
}

void MediaView::applyModuleSettings()
{
    const auto c=sizeCategory();
    applyVolume(m_settings->moduleVolume(c));
    m_audio->setMuted(m_settings->moduleMuted(c));
    m_controls->setVolume(m_settings->moduleVolume(c),m_settings->moduleMuted(c));
    m_controls->setLoop(m_settings->moduleLoopVideo());
}
void MediaView::resetPresentation()
{
    m_surface->resetPresentation();
    applyModuleSettings();
    showControls();
}

void MediaView::applyVolume(double volume)
{
    // Perceptual (cubic) mapping feels linear to the ear.
    const float linear = QAudio::convertVolume(float(volume), QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
    m_audio->setVolume(linear);
}

void MediaView::toggleMute()
{
    const bool muted = !m_audio->isMuted();
    m_audio->setMuted(muted);
    m_settings->setModuleMuted(sizeCategory(),muted);
    m_controls->setVolume(m_settings->moduleVolume(sizeCategory()), muted);
}

void MediaView::layoutChildren()
{
    m_surface->setGeometry(rect());
    const int h = m_controls->preferredHeight();
    m_controls->setGeometry(0, height() - h, width(), h);
    m_controls->raise();
}

void MediaView::resizeEvent(QResizeEvent *event)
{
    ContentView::resizeEvent(event);
    layoutChildren();
    m_controls->setFullscreen(window()->isFullScreen());
}

void MediaView::showControls()
{
    m_controls->show();
    unsetCursor();
    scheduleHide();
}

void MediaView::scheduleHide()
{
    if (m_hasVideo) {
        m_hideTimer.start();
    } else {
        m_hideTimer.stop();
    }
}

void MediaView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_hasVideo) {
        if (!m_surface->hasFrame()) {
            p.fillRect(rect(), Qt::black);
            p.setPen(Qt::white);
            p.drawText(rect(), Qt::AlignCenter, tr_("Loading…"));
        }
        return; // VideoSurface paints
    }
    p.setClipPath(Style::windowShapeFor(this));
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!m_ready) {
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, tr_("Loading…"));
        return;
    }
    if (!m_cover.isNull()) {
        // Fill the window with the artwork (crop, keep proportions).
        const QSizeF target = QSizeF(m_cover.size()).scaled(QSizeF(size()), Qt::KeepAspectRatioByExpanding);
        QRectF r(QPointF(0, 0), target);
        r.moveCenter(QRectF(rect()).center());
        p.drawImage(r, m_cover);
    } else {
        p.fillRect(rect(), Style::windowBackground(this));
        Style::paintAudioPlaceholder(p, QRectF(rect()).adjusted(4, 4, -4, -4), Style::isDark(this));
    }
}

bool MediaView::eventFilter(QObject *watched, QEvent *event)
{
    // The surface covers the view. Deliver its pointer events explicitly;
    // QWidget propagation depends on the child's accepted state and is unreliable.
    if (watched == m_surface) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: mousePressEvent(static_cast<QMouseEvent*>(event)); return event->isAccepted();
        case QEvent::MouseButtonRelease: mouseReleaseEvent(static_cast<QMouseEvent*>(event)); return event->isAccepted();
        case QEvent::MouseButtonDblClick: mouseDoubleClickEvent(static_cast<QMouseEvent*>(event)); return event->isAccepted();
        case QEvent::MouseMove: mouseMoveEvent(static_cast<QMouseEvent*>(event)); return event->isAccepted();
        default: break;
        }
    }
    if (watched == m_surface && event->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent *>(event));
        return event->isAccepted();
    }
    if ((watched == m_surface || watched == m_controls) && (event->type() == QEvent::MouseMove || event->type() == QEvent::Enter)) {
        showControls();
    }
    return ContentView::eventFilter(watched, event);
}

void MediaView::wheelEvent(QWheelEvent *event)
{
    if (!m_hasVideo || (m_controls->isVisible() && m_controls->geometry().contains(event->position().toPoint()))) {
        event->ignore(); return;
    }
    const qreal delta = event->angleDelta().y() ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 60.0;
    m_surface->zoomAt(m_surface->zoom() * std::pow(2.0, delta / 4.0), event->position());
    event->accept();
}

void MediaView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragged = false;
        m_panning = m_hasVideo && m_surface->zoom() > 1 && !(event->modifiers() & Qt::AltModifier);
        m_dragCandidate = !m_panning;
        m_lastPan = event->position();
        m_pressPos = event->position();
        event->accept();
        return;
    }
    event->ignore();
}

void MediaView::mouseMoveEvent(QMouseEvent *event)
{
    showControls();
    if ((event->buttons() & Qt::LeftButton) && (event->position() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) m_dragged = true;
    if (m_panning) {
        m_surface->panBy(event->position() - m_lastPan);
        m_lastPan = event->position(); event->accept(); return;
    }
    if (m_dragCandidate && (event->buttons() & Qt::LeftButton)) {
        if ((event->position() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_dragCandidate = false;
            requestWindowMove();
        }
        event->accept();
        return;
    }
    event->ignore();
}

void MediaView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (m_dragCandidate || m_panning) && !m_dragged
        && (event->position() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        m_clickPosition = event->position();
        m_clickTimer.start(QApplication::doubleClickInterval());
    }
    m_panning = m_dragCandidate = false;
    event->accept();
}

void MediaView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_hasVideo) {
        m_clickTimer.stop();
        m_dragged = true;
        Q_EMIT fullscreenRequested();
        event->accept();
        return;
    }
    event->ignore();
}

void MediaView::leaveEvent(QEvent *event)
{
    scheduleHide();
    ContentView::leaveEvent(event);
}

bool MediaView::handleKey(QKeyEvent *event)
{
    if (!m_hasVideo) return false;
    if (event->key() == Qt::Key_R) { m_surface->rotateLeft(); return true; }
    if (event->key() == Qt::Key_F) { Q_EMIT fullscreenRequested(); return true; }
    if (event->key() == Qt::Key_0) { m_surface->zoomAt(1, QRectF(rect()).center()); return true; }
    return false;
}

} // namespace KDEGlance
