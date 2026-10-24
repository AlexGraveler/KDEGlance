# SPDX-License-Identifier: GPL-3.0-or-later
"""Lazy, independent audio preview. No changes to the image Canvas."""
import math
import os
from pathlib import Path
from PyQt6.QtCore import Qt, QPoint, QEvent, QTimer, QSettings, QUrl, QPointF, QRectF, pyqtSignal, QThreadPool, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QPainter, QColor, QLinearGradient, QFont, QImage
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QSlider, QToolTip, QApplication, QGraphicsOpacityEffect, QSizePolicy
from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput
from audio_metadata import MetadataJob
from window import PreviewWindow
from visuals import draw_icon, draw_audio_placeholder


def time_text(milliseconds):
    if milliseconds is None or milliseconds < 0:
        return "--:--"
    seconds = int(milliseconds)//1000
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    return f"{hours:02}:{minutes:02}:{seconds:02}" if hours else f"{minutes:02}:{seconds:02}"


class SeekBar(QWidget):
    seekRequested = pyqtSignal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.duration_ms, self.position_ms, self.dragging = 0, 0, False
        self.setFixedHeight(28)
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setEnabled(False)
        self.setAccessibleName("Перемотка")

    def at(self, x):
        fraction = max(0.0, min(1.0, (x-8)/max(1, self.width()-16)))
        return round(fraction*self.duration_ms)

    def position(self, value):
        if not self.dragging:
            self.position_ms = value
            self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        track = QRectF(8, self.height()/2-4, max(1, self.width()-16), 8)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(255, 255, 255, 70))
        painter.drawRoundedRect(track, 4, 4)
        fraction = min(1, max(0, self.position_ms/max(1, self.duration_ms)))
        x = track.left()+track.width()*fraction
        painter.setBrush(QColor("#fafafa") if self.isEnabled() else QColor("#777777"))
        painter.drawRoundedRect(QRectF(track.left(), track.top(), track.width()*fraction, 8), 4, 4)
        painter.drawEllipse(QPointF(x, self.height()/2), 7, 7)

    def seek(self, event):
        self.position_ms = self.at(event.position().x())
        self.seekRequested.emit(self.position_ms)
        self.update()
        event.accept()

    def mousePressEvent(self, event):
        if self.isEnabled() and event.button() == Qt.MouseButton.LeftButton:
            self.dragging = True
            self.seek(event)

    def mouseMoveEvent(self, event):
        if self.dragging:
            self.seek(event)
        elif self.isEnabled():
            QToolTip.showText(event.globalPosition().toPoint(), time_text(self.at(event.position().x())), self)

    def mouseReleaseEvent(self, event):
        if self.dragging and event.button() == Qt.MouseButton.LeftButton:
            self.seek(event)
            self.dragging = False


class IconButton(QPushButton):
    def __init__(self, kind, parent=None):
        super().__init__(parent)
        self.kind, self.playing = kind, False
        self.setFixedSize(42, 42) if kind == "play" else self.setFixedSize(30, 32)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setAccessibleName("Play/Pause" if kind == "play" else "Громкость")
        self.setToolTip("Пауза / воспроизведение" if kind == "play" else "Громкость")

    def paintEvent(self, event):
        painter = QPainter(self)
        target = QRectF((self.width()-24)/2, (self.height()-24)/2, 24, 24)
        draw_icon(painter, self.kind, target, self.playing)



class ControlSurface(QWidget):
    """Consume unused control-space clicks instead of bubbling to the cover."""
    def mousePressEvent(self, event):
        event.accept()

    def mouseMoveEvent(self, event):
        event.accept()

    def mouseReleaseEvent(self, event):
        event.accept()


class VolumePanel(ControlSurface):
    """Transparent volume controls over the artwork; consume unused clicks."""


class Artwork(QWidget):
    closeRequested = pyqtSignal()
    navigateRequested = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.image, self.error = QImage(), ""
        self.controls_height = 64
        self.press_position, self.dragged = None, False

    def paintEvent(self, event):
        painter = QPainter(self)
        cover = QRectF(self.rect())
        if not self.image.isNull():
            edge = min(self.image.width(), self.image.height())
            source = QRectF((self.image.width()-edge)/2, (self.image.height()-edge)/2, edge, edge)
            painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            painter.drawImage(cover, self.image, source)
        else:
            draw_audio_placeholder(painter, cover)
        shade = QLinearGradient(0, self.height()-104, 0, self.height())
        opacity = getattr(self, "controls_opacity", 1)
        shade.setColorAt(0, QColor(0, 0, 0, 0))
        shade.setColorAt(.45, QColor(8, 12, 19, round(95*opacity)))
        shade.setColorAt(1, QColor(8, 12, 19, round(205*opacity)))
        painter.fillRect(self.rect(), shade)
        if self.error:
            painter.fillRect(self.rect(), QColor(10, 16, 24, 170))
            painter.setPen(QColor("#ffffff"))
            painter.setFont(QFont("sans-serif", 10))
            painter.drawText(self.rect().adjusted(18, 18, -18, -self.controls_height-12),
                             Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap, self.error)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.closeRequested.emit()
        elif event.button() == Qt.MouseButton.LeftButton:
            self.press_position, self.dragged = QPointF(event.position()), False
        event.accept()

    def update_gesture(self, position):
        if self.press_position is not None:
            self.dragged |= ((position-self.press_position).manhattanLength()
                             >= QApplication.startDragDistance())

    def mouseMoveEvent(self, event):
        self.update_gesture(event.position())
        event.accept()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.update_gesture(event.position())
            clicked = self.press_position is not None and not self.dragged
            self.press_position, self.dragged = None, False
            useful = self.rect().adjusted(0, 0, 0, -self.controls_height)
            if clicked and useful.contains(event.position().toPoint()):
                fraction = event.position().x()/max(1, self.width())
                if fraction < .2:
                    self.navigateRequested.emit(-1)
                elif fraction >= .8:
                    self.navigateRequested.emit(1)
                else:
                    self.closeRequested.emit()
        event.accept()


class AudioWindow(PreviewWindow):
    def __init__(self, close_callback, navigate_callback=None):
        super().__init__(close_callback, navigate_callback or (lambda _: None))
        old_canvas = self.canvas
        self.layout().removeWidget(old_canvas)
        old_canvas.hide()
        old_canvas.deleteLater()
        self.canvas = Artwork(self)
        self.canvas.closeRequested.connect(close_callback)
        self.canvas.navigateRequested.connect(self.navigate_callback)
        self.layout().addWidget(self.canvas)
        self.phase, self.path, self.error = "idle", "", ""
        self.seek_wheel_remainder = 0.0
        self.generation, self.metadata_jobs = 0, {}
        self.metadata_pool = QThreadPool(self)
        self.metadata_pool.setMaxThreadCount(1)
        config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home()/".config")/"kdeglance2/audio.ini"
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
        from interactions import install_interactions
        install_interactions(self)

    def present(self, screen, token=""):
        super().present(screen, token)
        self.resize_audio(screen)

    def resize_audio(self, screen):
        area = screen.availableGeometry()
        edge = max(1, min(760, area.width()-32, area.height()-32))
        self.apply_size(edge, edge)

    def open_audio(self, path):
        self.stop_audio()
        self.seek_wheel_remainder = 0.0
        self.path, self.phase, self.error = path, "loading", ""
        self.canvas.image, self.canvas.error = QImage(), ""
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
        job = MetadataJob(self.generation, path)
        self.metadata_jobs[self.generation] = job
        job.signals.done.connect(self.metadata_loaded)
        self.metadata_pool.start(job)
        self.player.setSource(QUrl.fromLocalFile(path))
        self.player.play()

    def metadata_loaded(self, generation, result):
        self.metadata_jobs.pop(generation, None)
        if generation != self.generation or self.phase == "idle":
            return
        self.canvas.image = result["cover"]
        self.canvas.update()

    def stop_audio(self):
        self.generation += 1
        for job in self.metadata_jobs.values():
            job.cancelled.set()
        self.dismiss_volume_panel()
        self.canvas.press_position = None
        self.player.stop()
        self.player.setSource(QUrl())
        self.seek_bar.dragging = False
        self.seek_bar.setEnabled(False)
        self.phase = "idle"
        self.save_volume()

    def hideEvent(self, event):
        if hasattr(self, "player"):
            self.stop_audio()
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

    def playback_changed(self, state):
        self.play_button.playing = state == QMediaPlayer.PlaybackState.PlayingState
        self.play_button.update()
        if state in (QMediaPlayer.PlaybackState.PlayingState, QMediaPlayer.PlaybackState.PausedState):
            self.phase = "ready"

    def status_changed(self, status):
        if status == QMediaPlayer.MediaStatus.InvalidMedia:
            self.player_error(None, self.player.errorString())
        elif status == QMediaPlayer.MediaStatus.EndOfMedia:
            self.phase = "ready"

    def player_error(self, _error, text):
        self.player.stop()
        self.phase, self.error = "error", text or "Не удалось воспроизвести аудио"
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
            self.position_volume_panel()

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
        pass

    def snapshot(self):
        return {"phase": self.phase, "error": self.error,
                "playing": self.player.playbackState() == QMediaPlayer.PlaybackState.PlayingState,
                "playback_state": self.player.playbackState().name,
                "position_ms": self.player.position(), "duration_ms": self.player.duration(),
                "volume": round(self.output.volume(), 5), "seekable": self.player.isSeekable(),
                "source_empty": self.player.source().isEmpty(), "has_cover": not self.canvas.image.isNull(),
                "volume_panel_visible": self.volume_panel.isVisible()}
