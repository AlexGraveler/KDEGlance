# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent video preview with the accepted music controls, left unchanged."""
import math
import os
from pathlib import Path
from PyQt6.QtCore import Qt, QPoint, QPointF, QEvent, QTimer, QSettings, QUrl, QRectF, QSize, QPropertyAnimation, QEasingCurve, pyqtSignal, pyqtSlot
from PyQt6.QtGui import QPainter, QColor, QLinearGradient, QFont
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QSlider, QGraphicsOpacityEffect, QSizePolicy
from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput, QVideoSink, QVideoFrame
from interactions import ActionButton, install_interactions
from window import PreviewWindow
from audio import Artwork, SeekBar, IconButton, ControlSurface, VolumePanel, time_text


class VideoCanvas(Artwork):
    zoomChanged = pyqtSignal()
    def __init__(self, parent=None):
        super().__init__(parent)
        self.frame = QVideoFrame()
        self.zoom, self.base_scale, self.pan = 1.0, 1.0, QPointF()
        self.dimensions = QSize()
        self.last = None
        self.manual_rotation = 0
        self.paint_options = QVideoFrame.PaintOptions()
        self.paint_options.aspectRatioMode = Qt.AspectRatioMode.KeepAspectRatio
        self.paint_options.backgroundColor = QColor("#0b1017")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), self.paint_options.backgroundColor)
        if self.frame.isValid():
            rect = self.image_rect()
            painter.save()
            painter.translate(rect.center())
            painter.rotate(self.manual_rotation)
            width, height = rect.width(), rect.height()
            if self.manual_rotation % 180:
                width, height = height, width
            self.frame.paint(painter, QRectF(-width/2,-height/2,width,height), self.paint_options)
            painter.restore()
        elif not self.error:
            painter.setPen(QColor("#8793a2"))
            painter.setFont(QFont("sans-serif", 13))
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, "Загрузка видео…")
        shade = QLinearGradient(0, self.height()-104, 0, self.height())
        shade.setColorAt(0, QColor(0, 0, 0, 0))
        opacity = getattr(self, "controls_opacity", 1)
        shade.setColorAt(.45, QColor(8, 12, 19, round(95*opacity)))
        shade.setColorAt(1, QColor(8, 12, 19, round(205*opacity)))
        painter.fillRect(self.rect(), shade)
        if self.error:
            painter.fillRect(self.rect(), QColor(10, 16, 24, 170))
            painter.setPen(QColor("#ffffff"))
            painter.setFont(QFont("sans-serif", 10))
            painter.drawText(self.rect().adjusted(18, 18, -18, -self.controls_height-12),
                             Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap, self.error)


    def image_rect(self):
        width = self.dimensions.width()*self.base_scale*self.zoom
        height = self.dimensions.height()*self.base_scale*self.zoom
        return QRectF((self.width()-width)/2+self.pan.x(), (self.height()-height)/2+self.pan.y(), width, height)

    def clamp_pan(self):
        rect = self.image_rect()
        x, y = max(0,(rect.width()-self.width())/2), max(0,(rect.height()-self.height())/2)
        self.pan = QPointF(max(-x,min(x,self.pan.x())),max(-y,min(y,self.pan.y())))

    def zoom_wheel(self, event):
        if not self.frame.isValid():
            return
        delta = event.angleDelta().y() or event.pixelDelta().y()
        old = self.image_rect()
        zoom = max(1.0,min(32.0,self.zoom*(1.15**(delta/120))))
        top = event.position()-(event.position()-old.topLeft())*(zoom/self.zoom)
        self.zoom = zoom
        self.pan += top-self.image_rect().topLeft()
        self.zoomChanged.emit()
        self.clamp_pan()
        self.update()
        event.accept()

    def mousePressEvent(self, event):
        self.last = QPointF(event.position())
        super().mousePressEvent(event)

    def update_gesture(self, position):
        super().update_gesture(position)
        if self.dragged and self.zoom > 1 and self.last is not None:
            self.pan += position-self.last
            self.clamp_pan()
            self.update()
        self.last = QPointF(position)


class VideoWindow(PreviewWindow):
    def __init__(self, close_callback, navigate_callback=None):
        super().__init__(close_callback, navigate_callback or (lambda _: None))
        old_canvas = self.canvas
        self.layout().removeWidget(old_canvas)
        old_canvas.hide()
        old_canvas.deleteLater()
        self.canvas = VideoCanvas(self)
        self.canvas.closeRequested.connect(close_callback)
        self.canvas.navigateRequested.connect(self.navigate_callback)
        self.canvas.zoomChanged.connect(self.resize_for_video_zoom)
        self.layout().addWidget(self.canvas)
        self.phase, self.path, self.error = "idle", "", ""
        self.frames_received = 0
        self.video_dimensions = QSize()
        self.manual_rotation = 0
        self.seek_wheel_remainder = 0.0
        config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home()/".config")/"kdeglance2/video.ini"
        self.settings = QSettings(str(config), QSettings.Format.IniFormat)
        try:
            volume = float(self.settings.value("volume", 0.5))
            if not math.isfinite(volume):
                volume = 0.5
        except (ValueError, TypeError):
            volume = 0.5
        self.output = QAudioOutput(self)
        self.volume_level = max(0.0, min(1.0, volume))
        self.output.setVolume(self.volume_level)
        self.player = QMediaPlayer(self)
        self.player.setAudioOutput(self.output)
        self.video_sink = QVideoSink(self)
        self.player.setVideoSink(self.video_sink)
        self.video_sink.videoFrameChanged.connect(self.frame_changed)
        self.save_timer = QTimer(self)
        self.save_timer.setSingleShot(True)
        self.save_timer.timeout.connect(self.save_volume)
        cover_layout = QVBoxLayout(self.canvas)
        cover_layout.setContentsMargins(0, 0, 0, 0)
        cover_layout.setSpacing(0)
        cover_layout.addStretch()
        self.controls = ControlSurface(self.canvas)
        self.controls.setFixedHeight(64)
        layout = QHBoxLayout(self.controls)
        layout.setContentsMargins(12, 10, 12, 10)
        layout.setSpacing(7)
        self.play_button, self.speaker_button = IconButton("play"), IconButton("volume")
        self.elapsed, self.total = QLabel("00:00"), QLabel("--:--")
        for label in (self.elapsed, self.total):
            label.setStyleSheet("color:#f7f8fa;background:transparent;font-size:12px;")
            label.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Preferred)
        self.seek_bar = SeekBar(self.controls)
        self.seek_bar.setMinimumWidth(44)
        layout.addWidget(self.play_button)
        layout.addWidget(self.elapsed)
        layout.addWidget(self.seek_bar, 1)
        layout.addWidget(self.total)
        self.rotate_button = ActionButton("rotate", self.controls)
        self.fullscreen_button = ActionButton("fullscreen", self.controls)
        self.rotate_button.clicked.connect(self.rotate_video)
        self.fullscreen_button.clicked.connect(self.toggle_fullscreen)
        layout.addWidget(self.rotate_button)
        layout.addWidget(self.fullscreen_button)
        self.speaker_slot = ControlSurface(self.controls)
        self.speaker_slot.setFixedSize(56,44)
        speaker_layout = QHBoxLayout(self.speaker_slot)
        speaker_layout.setContentsMargins(0,0,0,0)
        speaker_layout.addWidget(self.speaker_button,0,Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(self.speaker_slot)
        cover_layout.addWidget(self.controls)
        self.volume_panel = VolumePanel(self.canvas)
        self.volume_panel.setFixedWidth(56)
        self.volume_panel.resize(56, 212)
        panel_layout = QVBoxLayout(self.volume_panel)
        panel_layout.setContentsMargins(9, 12, 9, 72)
        panel_layout.setSpacing(10)
        self.percent = QLabel(self.volume_panel)
        self.percent.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.percent.setStyleSheet("color:#f1f4f8;background:transparent;font-size:13px;")
        panel_layout.addWidget(self.percent)
        self.volume_slider = QSlider(Qt.Orientation.Vertical, self.volume_panel)
        self.volume_slider.setRange(0, 100)
        self.volume_slider.setValue(round(self.output.volume()*100))
        self.volume_slider.setAccessibleName("Громкость")
        self.volume_slider.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.volume_slider.setStyleSheet("""
            QSlider { background: transparent; }
            QSlider::groove:vertical { background: rgba(255,255,255,45); width: 6px; border-radius: 3px; }
            QSlider::add-page:vertical { background: #f1f4f8; border-radius: 3px; }
            QSlider::handle:vertical { background: white; height: 14px; margin: 0 -5px; border-radius: 7px; }
        """)
        panel_layout.addWidget(self.volume_slider, 1, Qt.AlignmentFlag.AlignHCenter)
        self.volume_opacity = QGraphicsOpacityEffect(self.volume_panel)
        self.volume_panel.setGraphicsEffect(self.volume_opacity)
        self.volume_opacity.setOpacity(1)
        self.volume_fade = QPropertyAnimation(self.volume_opacity, b"opacity", self)
        self.volume_fade.setDuration(220)
        self.volume_fade.setEasingCurve(QEasingCurve.Type.OutQuad)
        self.volume_fade.finished.connect(self.finish_volume_fade)
        self.volume_panel.hide()
        self.percent_timer = QTimer(self)
        self.percent_timer.setSingleShot(True)
        self.percent_timer.timeout.connect(self.hide_volume_panel)
        self.play_button.clicked.connect(self.toggle_play)
        self.speaker_button.clicked.connect(self.toggle_volume_panel)
        self.volume_slider.valueChanged.connect(lambda value: self.set_volume(value/100))
        self.seek_bar.seekRequested.connect(self.player.setPosition)
        self.player.positionChanged.connect(self.position_changed)
        self.player.durationChanged.connect(self.duration_changed)
        self.player.seekableChanged.connect(lambda _: self.update_seekable())
        self.player.playbackStateChanged.connect(self.playback_changed)
        self.player.mediaStatusChanged.connect(self.status_changed)
        self.player.errorOccurred.connect(self.player_error)
        for widget in [self.canvas, *self.canvas.findChildren(QWidget)]:
            widget.setFocusPolicy(Qt.FocusPolicy.NoFocus)
            widget.installEventFilter(self)
        install_interactions(self)

    def present(self, screen, token=""):
        if not self.isVisible() and self.isFullScreen():
            self.setWindowState(Qt.WindowState.WindowNoState)
        super().present(screen, token)
        self.resize_video(screen)

    def resize_video(self, screen):
        if self.isFullScreen():
            self.layout().activate()
            self.update_video_scale()
            return
        area = screen.availableGeometry()
        size = self.video_dimensions
        if size.isEmpty():
            self.apply_size(min(560, max(1, area.width()-32)), min(360, max(1, area.height()-32)))
            return
        # Match photo sizing: native logical size, downscale only to fit screen.
        scale = min(1.0, max(1, area.width()-32)/size.width(),
                    max(1, area.height()-32)/size.height())
        self.apply_size(max(1, int(size.width()*scale)), max(1, int(size.height()*scale)))
        self.canvas.base_scale = scale
        if self.canvas.zoom > 1:
            self.resize_for_video_zoom()

    def update_video_scale(self):
        if not self.video_dimensions.isEmpty() and self.canvas.zoom == 1:
            scale = min(self.canvas.width()/self.video_dimensions.width(), self.canvas.height()/self.video_dimensions.height())
            self.canvas.base_scale = scale if self.isFullScreen() else min(1.0,scale)

    @pyqtSlot()
    def resize_for_video_zoom(self):
        if self.isFullScreen():
            return
        rect = self.canvas.image_rect()
        area = self.screen().availableGeometry()
        self.apply_size(max(1,min(int(rect.width()),area.width()-32)), max(1,min(int(rect.height()),area.height()-32)))
        self.layout().activate()
        self.canvas.pan += rect.topLeft()-self.canvas.image_rect().topLeft()
        self.canvas.clamp_pan()

    @pyqtSlot()
    def rotate_video(self):
        self.manual_rotation = (self.manual_rotation-90) % 360
        self.canvas.manual_rotation = self.manual_rotation
        if self.canvas.frame.isValid():
            self.video_dimensions = self.display_size(self.canvas.frame)
            if self.manual_rotation % 180:
                self.video_dimensions.transpose()
            self.canvas.dimensions = self.video_dimensions
            self.fit()

    @pyqtSlot()
    def toggle_fullscreen(self):
        self.canvas.zoom, self.canvas.pan = 1.0, QPointF()
        self.target_size = None
        self.size_timer.stop()
        self.setMinimumSize(1,1)
        self.setMaximumSize(16777215,16777215)
        if self.isFullScreen():
            self.showNormal()
            self.resize_video(self.screen())
        else:
            self.showFullScreen()
        self.layout().activate()
        self.update_video_scale()
        self.canvas.update()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key.Key_Escape and self.isFullScreen():
            self.toggle_fullscreen()
            event.accept()
        elif event.key() == Qt.Key.Key_F:
            self.toggle_fullscreen()
            event.accept()
        else:
            super().keyPressEvent(event)

    @staticmethod
    def display_size(frame):
        size = frame.surfaceFormat().viewport().size()
        if size.isEmpty():
            size = frame.size()
        rotation = frame.rotation().value + frame.surfaceFormat().rotation().value
        if rotation % 180:
            size.transpose()
        return size

    def open_video(self, path):
        self.stop_video()
        self.path, self.phase, self.error = path, "loading", ""
        self.canvas.frame, self.canvas.error = QVideoFrame(), ""
        self.frames_received = 0
        self.video_dimensions = QSize()
        self.manual_rotation, self.seek_wheel_remainder = 0, 0.0
        self.canvas.manual_rotation = 0
        self.canvas.zoom, self.canvas.pan = 1.0, QPointF()
        self.canvas.update()
        self.seek_bar.dragging = False
        self.seek_bar.position_ms, self.seek_bar.duration_ms = 0, 0
        self.seek_bar.setEnabled(False)
        self.elapsed.setText("00:00")
        self.total.setText("--:--")
        self.volume_panel.hide()
        self.setWindowTitle(Path(path).name+" — KDEGlance")
        if not Path(path).is_file():
            self.player_error(None, "Файл не найден или недоступен")
            return
        self.player.setSource(QUrl.fromLocalFile(path))
        self.player.play()

    @pyqtSlot(QVideoFrame)
    def frame_changed(self, frame):
        if self.phase == "idle" or self.player.source().isEmpty():
            return
        if frame.isValid():
            # Keep just the newest frame; Qt paints rotation/aspect correctly.
            self.canvas.frame = QVideoFrame(frame)
            size = self.display_size(self.canvas.frame)
            if self.manual_rotation % 180:
                size.transpose()
            self.canvas.dimensions = size
            if size != self.video_dimensions:
                self.video_dimensions = size
                self.resize_video(self.screen())
            self.frames_received += 1
            self.canvas.update()

    def stop_video(self):
        self.dismiss_volume_panel()
        self.canvas.press_position = None
        self.player.stop()
        self.player.setSource(QUrl())
        self.canvas.frame = QVideoFrame()
        self.canvas.update()
        self.seek_bar.dragging = False
        self.seek_bar.setEnabled(False)
        self.phase = "idle"
        self.save_volume()

    def hideEvent(self, event):
        if hasattr(self, "player"):
            self.stop_video()
        super().hideEvent(event)

    def toggle_play(self):
        if self.player.source().isEmpty() or self.phase == "error":
            return
        if self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState:
            self.player.pause()
        else:
            if self.player.mediaStatus() == QMediaPlayer.MediaStatus.EndOfMedia:
                self.player.setPosition(0)
            self.player.play()

    @pyqtSlot(QMediaPlayer.PlaybackState)
    def playback_changed(self, state):
        self.play_button.playing = state == QMediaPlayer.PlaybackState.PlayingState
        self.play_button.update()
        if state in (QMediaPlayer.PlaybackState.PlayingState, QMediaPlayer.PlaybackState.PausedState):
            self.phase = "ready"

    @pyqtSlot(QMediaPlayer.MediaStatus)
    def status_changed(self, status):
        if status == QMediaPlayer.MediaStatus.InvalidMedia:
            self.player_error(None, self.player.errorString())
        elif status in (QMediaPlayer.MediaStatus.LoadedMedia, QMediaPlayer.MediaStatus.BufferedMedia) and not self.player.hasVideo():
            self.player_error(None, "В файле нет видеодорожки")
        elif status == QMediaPlayer.MediaStatus.EndOfMedia:
            self.phase = "ready"

    @pyqtSlot(QMediaPlayer.Error, str)
    def player_error(self, _error, text):
        self.player.stop()
        self.phase, self.error = "error", text or "Не удалось воспроизвести видео"
        self.canvas.error = self.error
        self.canvas.update()
        self.update_seekable()

    def position_changed(self, position):
        self.seek_bar.position(position)
        self.elapsed.setText(time_text(self.seek_bar.position_ms))

    def duration_changed(self, duration):
        self.seek_bar.duration_ms = max(0, duration)
        self.total.setText(time_text(duration) if duration > 0 else "--:--")
        self.update_seekable()

    def update_seekable(self):
        self.seek_bar.setEnabled(self.player.isSeekable() and self.player.duration() > 0 and self.phase != "error")

    def save_volume(self):
        self.save_timer.stop()
        self.settings.setValue("volume", float(self.output.volume()))
        self.settings.sync()

    def set_volume(self, value):
        value = max(0.0, min(1.0, value))
        self.volume_level = value
        self.output.setVolume(value)
        self.volume_slider.blockSignals(True)
        self.volume_slider.setValue(round(value*100))
        self.volume_slider.blockSignals(False)
        self.percent.setText(f"{round(value*100)}%")
        self.show_volume_panel()
        self.save_timer.start(250)

    def position_volume_panel(self):
        # One continuous surface rises from the existing speaker slot.
        anchor = self.speaker_slot.mapTo(self.canvas,QPoint())
        top = self.controls.mapTo(self.canvas,QPoint()).y()
        height = max(1,min(200,top-20))
        bottom = min(self.canvas.height()-4,anchor.y()+self.speaker_slot.height()+6)
        self.volume_panel.setGeometry(anchor.x(),max(0,top-height),self.speaker_slot.width(),bottom-(top-height))

    def show_volume_panel(self):
        self.volume_fade.stop()
        self.volume_opacity.setOpacity(1)
        self.percent.setText(f"{round(self.output.volume()*100)}%")
        self.position_volume_panel()
        self.volume_panel.raise_()
        self.volume_panel.show()
        self.controls.raise_()
        self.percent_timer.start(1500)

    def hide_volume_panel(self):
        if self.volume_slider.isSliderDown():
            self.percent_timer.start(300)
            return
        self.volume_fade.setStartValue(self.volume_opacity.opacity())
        self.volume_fade.setEndValue(0)
        self.volume_fade.start()

    def finish_volume_fade(self):
        if self.volume_opacity.opacity() <= 0:
            self.volume_panel.hide()

    def dismiss_volume_panel(self):
        self.percent_timer.stop()
        self.volume_fade.stop()
        self.volume_panel.hide()
        self.volume_opacity.setOpacity(1)

    def toggle_volume_panel(self):
        if self.volume_panel.isVisible():
            self.dismiss_volume_panel()
        else:
            self.show_volume_panel()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, "volume_panel"):
            # Keep the same controls usable on native-size portrait videos.
            compact = self.width() < 340
            self.controls.layout().setContentsMargins(6 if compact else 12, 10, 6 if compact else 12, 10)
            self.controls.layout().setSpacing(4 if compact else 7)
            self.play_button.setFixedSize(36 if compact else 42, 36 if compact else 42)
            self.elapsed.setVisible(not compact)
            self.rotate_button.setFixedWidth(24 if compact else 30)
            self.fullscreen_button.setFixedWidth(24 if compact else 30)
            self.speaker_slot.setFixedWidth(36 if compact else 56)
            self.volume_panel.setFixedWidth(36 if compact else 56)
            self.seek_bar.setMinimumWidth(16 if compact else 44)
            self.volume_panel.layout().setContentsMargins(2 if compact else 9, 12, 2 if compact else 9, 72)
            self.position_volume_panel()
            self.layout().activate()
            self.update_video_scale()
            self.canvas.clamp_pan()

    def volume_wheel(self, event):
        delta = event.angleDelta().y() or event.pixelDelta().y()
        if delta:
            # Keep fractional touchpad increments in double precision rather
            # than quantizing each event to the integer slider percentage.
            if abs(self.output.volume()-self.volume_level) > 1e-6:
                self.volume_level = self.output.volume()
            self.set_volume(self.volume_level+delta/120*0.05)
        event.accept()

    def eventFilter(self, watched, event):
        if isinstance(watched, QWidget):
            if event.type() == QEvent.Type.Wheel:
                if watched is self.speaker_button:
                    self.volume_wheel(event)
                elif watched is self.seek_bar:
                    if self.player.isSeekable():
                        self.seek_wheel_remainder += (event.angleDelta().y() or event.pixelDelta().y())/120
                        steps = math.trunc(self.seek_wheel_remainder)
                        self.seek_wheel_remainder -= steps
                        self.player.setPosition(max(0,min(self.player.duration(),self.player.position()+steps*1000)))
                    event.accept()
                elif watched is self.canvas:
                    self.canvas.zoom_wheel(event)
                else:
                    event.accept()
                return True
            if event.type() == QEvent.Type.KeyPress and event.key() in (Qt.Key.Key_Space, Qt.Key.Key_Escape):
                self.keyPressEvent(event)
                return True
        return super().eventFilter(watched, event)

    def wheelEvent(self, event):
        event.accept()

    def fit(self):
        self.canvas.zoom, self.canvas.pan = 1.0, QPointF()
        self.resize_video(self.screen())
        self.canvas.update()

    def snapshot(self):
        return {"phase": self.phase, "error": self.error,
                "playing": self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState,
                "playback_state": self.player.playbackState().name,
                "position_ms": self.player.position(), "duration_ms": self.player.duration(),
                "volume": round(self.output.volume(), 5), "seekable": self.player.isSeekable(),
                "source_empty": self.player.source().isEmpty(), "has_frame": self.canvas.frame.isValid(),
                "frames_received": self.frames_received,
                "has_video": self.player.hasVideo(), "has_audio": self.player.hasAudio(),
                "frame_size": [self.canvas.frame.width(), self.canvas.frame.height()],
                "display_size": [self.video_dimensions.width(), self.video_dimensions.height()],
                "zoom": self.canvas.zoom, "rotation": self.manual_rotation,
                "volume_panel_visible": self.volume_panel.isVisible()}
