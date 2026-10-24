# SPDX-License-Identifier: GPL-3.0-or-later
"""One bounded, nonmodal window; image panning never moves an overlay."""
import ctypes
from pathlib import Path
from PyQt6 import sip
from PyQt6.QtCore import Qt, QRectF, QPointF, QSize, QTimer, QEvent, QThreadPool, pyqtSignal, pyqtSlot
from PyQt6.QtGui import QColor, QPainter, QImage, QTransform
from PyQt6.QtWidgets import QApplication, QWidget, QVBoxLayout, QLayout


class Canvas(QWidget):
    closeRequested = pyqtSignal()
    navigateRequested = pyqtSignal(int)
    zoomChanged = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.image = QImage()
        self.zoom = 1.0
        self.base_scale = None
        self.pan = QPointF()
        self.last = None
        self.press_position = None
        self.dragged = False
        self.message = ""
        self.setMinimumSize(1, 1)

    def fit_scale(self):
        if self.image.isNull():
            return 1.0
        if self.base_scale is not None:
            return self.base_scale
        return min(self.width() / self.image.width(), self.height() / self.image.height(), 1.0)

    def image_rect(self):
        scale = self.fit_scale() * self.zoom
        w, h = self.image.width() * scale, self.image.height() * scale
        return QRectF((self.width()-w)/2 + self.pan.x(), (self.height()-h)/2 + self.pan.y(), w, h)

    def reset(self):
        self.zoom, self.pan, self.last = 1.0, QPointF(), None
        self.base_scale = None
        self.press_position = None
        self.dragged = False
        self.unsetCursor()
        self.update()

    def visible_image_rect(self):
        return self.image_rect().intersected(QRectF(self.rect())) if not self.image.isNull() else QRectF()

    def clamp_pan(self):
        if self.image.isNull():
            self.pan = QPointF()
            return
        r = self.image_rect()
        x = max(0.0, (r.width()-self.width())/2)
        y = max(0.0, (r.height()-self.height())/2)
        self.pan = QPointF(max(-x, min(x, self.pan.x())), max(-y, min(y, self.pan.y())))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#111111"))
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        if self.message:
            painter.setPen(QColor("#dce2eb"))
            painter.drawText(self.rect().adjusted(20, 20, -20, -20),
                             Qt.AlignmentFlag.AlignCenter | Qt.TextFlag.TextWordWrap, self.message)
        elif not self.image.isNull():
            painter.drawImage(self.image_rect(), self.image)

    def wheelEvent(self, event):
        if self.image.isNull():
            return
        delta = event.angleDelta().y() or event.pixelDelta().y()
        if not delta:
            return
        old = self.image_rect()
        new_zoom = max(1.0, min(32.0, self.zoom * (1.15 ** (delta/120.0))))
        ratio = new_zoom / self.zoom
        mouse = event.position()
        new_top = mouse - (mouse - old.topLeft()) * ratio
        self.zoom = new_zoom
        new_rect = self.image_rect()
        self.pan += new_top - new_rect.topLeft()
        self.zoomChanged.emit()
        self.clamp_pan()
        self.update()
        event.accept()

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.MiddleButton:
            self.reset_gesture()
            event.accept()
            self.closeRequested.emit()
            return
        if event.button() == Qt.MouseButton.LeftButton:
            self.reset_gesture()
            if self.visible_image_rect().contains(event.position()):
                self.press_position = QPointF(event.position())
                self.last = QPointF(event.position())
        event.accept()

    def reset_gesture(self):
        self.press_position, self.last, self.dragged = None, None, False
        self.unsetCursor()

    def update_gesture(self, position):
        if self.press_position is None:
            return
        if not self.dragged:
            self.dragged = ((position-self.press_position).manhattanLength()
                            >= QApplication.startDragDistance())
        if self.dragged and self.zoom > 1:
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.pan += position - self.last
            self.last = QPointF(position)
            self.clamp_pan()
            self.update()

    def mouseMoveEvent(self, event):
        if event.buttons() & Qt.MouseButton.LeftButton:
            self.update_gesture(event.position())
        event.accept()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            # Release may be delivered without a final MouseMove event.
            self.update_gesture(event.position())
            clicked = self.press_position is not None and not self.dragged
            self.reset_gesture()  # Clear before a slot hides or reloads us.
            visible = self.visible_image_rect()
            if clicked and visible.contains(event.position()):
                fraction = (event.position().x()-visible.left())/visible.width()
                if fraction < 0.2:
                    self.navigateRequested.emit(-1)
                elif fraction >= 0.8:
                    self.navigateRequested.emit(1)
                else:
                    self.closeRequested.emit()
        event.accept()

    def resizeEvent(self, event):
        self.clamp_pan()

    def mouseDoubleClickEvent(self, event):
        self.mousePressEvent(event)


class PreviewWindow(QWidget):
    animationError = pyqtSignal(str)
    outputChanged = pyqtSignal(object)
    exposed = pyqtSignal()
    focusLost = pyqtSignal()

    def __init__(self, close_callback, navigate_callback):
        super().__init__(None, Qt.WindowType.Window | Qt.WindowType.FramelessWindowHint)
        self.close_callback, self.navigate_callback = close_callback, navigate_callback
        self.activation_observed = False
        self.activation_epoch = 0
        self.target_size = None
        self.size_retries = 0
        self.size_timer = QTimer(self)
        self.size_timer.setSingleShot(True)
        self.size_timer.timeout.connect(self.reconcile_size)
        self.setWindowTitle("KDEGlance")
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.connected_handle = None
        self.native = None
        library = Path(__file__).with_name("libkdeglance_native.so")
        if library.exists():
            self.native = ctypes.CDLL(str(library))
            self.native.kdeglance_activate.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            self.native.kdeglance_activate.restype = None
        outer = QVBoxLayout(self)
        outer.setSizeConstraint(QLayout.SizeConstraint.SetNoConstraint)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        self.canvas = Canvas(self)
        self.canvas.closeRequested.connect(self.close_callback)
        self.canvas.navigateRequested.connect(self.navigate_callback)
        self.canvas.zoomChanged.connect(self.resize_for_zoom)
        outer.addWidget(self.canvas, 1)
        self.frames, self.frame_index, self.completed_loops, self.loops = [], 0, 0, -1
        self.animation = None
        self.animation_frame_count = self.animation_index = self.animation_displayed = 0
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.advance_frame)
        self.image_rotation, self.image_mirrored = 0, False
        if type(self) is PreviewWindow:
            from interactions import install_interactions
            install_interactions(self, photo=True)

    def transformed_frame(self):
        image = self.frames[self.frame_index][0]
        if self.image_rotation:
            image = image.transformed(QTransform().rotate(self.image_rotation))
        return image.mirrored(True, False) if self.image_mirrored else image

    @pyqtSlot()
    def rotate_image(self):
        if self.frames:
            self.image_rotation = (self.image_rotation-90) % 360
            self.refit_transformed_image()

    @pyqtSlot()
    def flip_image(self):
        if self.frames:
            self.image_mirrored = not self.image_mirrored
            self.refit_transformed_image()

    def refit_transformed_image(self):
        self.canvas.image = self.transformed_frame()
        self.canvas.reset()
        self.resize_to_image(self.canvas.image, self.screen())

    def fit(self):
        self.canvas.reset()
        if not self.canvas.image.isNull():
            self.resize_to_image(self.canvas.image, self.screen())

    def clear_content(self, message):
        self.stop_animation()
        self.frames = []
        self.canvas.image = QImage()
        self.canvas.message = message
        self.canvas.reset()

    def stop_animation(self):
        self.timer.stop()
        if self.animation:
            self.animation.cancel()
            self.animation = None

    def set_frames(self, frames, loops, animation_path=None, animation_frame_count=0):
        self.stop_animation()
        self.image_rotation, self.image_mirrored = 0, False
        self.frames, self.loops = frames, loops
        self.frame_index, self.completed_loops = 0, 0
        self.canvas.message = ""
        self.canvas.image = frames[0][0]
        self.canvas.reset()
        self.animation_frame_count = animation_frame_count
        self.animation_index = 0
        self.animation_displayed = 1
        if animation_path:
            from animation import AnimationStream
            self.animation = AnimationStream(animation_path, frames[0][0].size(), loops)
            QThreadPool.globalInstance().start(self.animation)
            self.timer.start(frames[0][1])
        elif len(frames) > 1:
            self.timer.start(frames[0][1])

    def advance_frame(self):
        if not self.frames or not self.isVisible():
            return
        if self.animation:
            item = self.animation.take()
            if item is None:
                state = self.animation.snapshot()
                if not state['finished']:
                    self.timer.start(10)
                elif state['error']:
                    self.clear_content(state['error'])
                    self.animationError.emit(state['error'])
                return
            image, delay, self.animation_index, self.completed_loops = item
            self.frames = [(image, delay)]
            self.animation_displayed += 1
            self.canvas.image = self.transformed_frame()
            self.canvas.update()
            self.timer.start(delay)
            return
        self.frame_index += 1
        if self.frame_index == len(self.frames):
            self.completed_loops += 1
            if self.loops >= 0 and self.completed_loops > self.loops:
                return
            self.frame_index = 0
        self.canvas.image = self.transformed_frame()
        self.canvas.update()
        self.timer.start(self.frames[self.frame_index][1])

    def resize_to_image(self, image, screen):
        available = screen.availableGeometry()
        max_w, max_h = max(1, available.width()-32), max(1, available.height()-32)
        # No chrome or minimum preview size; only rounding to whole logical pixels.
        ratio = min(max_w/image.width(), max_h/image.height(), 1.0)
        w, h = max(1, int(image.width()*ratio)), max(1, int(image.height()*ratio))
        # Keep the initial scale independent of subsequent viewport growth.
        # Otherwise resizing the window would apply the zoom a second time.
        self.canvas.base_scale = min(w/image.width(), h/image.height(), 1.0)
        # Publish matching native min/max constraints as well as requesting a
        # size. A late Wayland configure for the loading surface must not leave
        # an image-sized canvas in the old, smaller viewport.
        self.apply_size(w, h)
        if self.windowHandle() and self.windowHandle().screen() != screen:
            self.windowHandle().setScreen(screen)
        if self.canvas.zoom > 1:
            self.resize_for_zoom(screen)

    def resize_for_zoom(self, screen=None):
        if self.canvas.image.isNull() or self.canvas.base_scale is None:
            return
        available = (screen or self.screen()).availableGeometry()
        old = self.canvas.image_rect()
        self.apply_size(max(1, min(int(old.width()), max(1, available.width()-32))),
                        max(1, min(int(old.height()), max(1, available.height()-32))))
        self.layout().activate()
        # Retain the wheel anchor where the available panning range allows it.
        new = self.canvas.image_rect()
        self.canvas.pan += old.topLeft()-new.topLeft()
        self.canvas.clamp_pan()
        self.canvas.update()

    def present(self, screen, token=""):
        if not self.isVisible():
            self.apply_size(min(560, screen.availableGeometry().width()),
                            min(360, screen.availableGeometry().height()))
        self.winId()  # Create a normal toplevel before selecting its output.
        if self.connected_handle is not self.windowHandle():
            self.connected_handle = self.windowHandle()
            self.connected_handle.screenChanged.connect(self.outputChanged.emit)
            self.connected_handle.installEventFilter(self)
        if self.windowHandle().screen() != screen:
            self.windowHandle().setScreen(screen)
        self.show()
        self.setFocus(Qt.FocusReason.ActiveWindowFocusReason)
        if self.native:
            self.native.kdeglance_activate(int(sip.unwrapinstance(self.windowHandle())), token.encode())
        else:
            self.windowHandle().requestActivate()

    def eventFilter(self, watched, event):
        if event.type() == QEvent.Type.Expose and watched is self.windowHandle() and watched.isExposed():
            self.exposed.emit()
        if event.type() == QEvent.Type.Resize and watched is self.windowHandle():
            self.schedule_size_check()
        return super().eventFilter(watched, event)

    def apply_size(self, width, height):
        self.target_size = QSize(width, height)
        self.size_retries = 0
        self.setFixedSize(self.target_size)
        self.schedule_size_check()

    def schedule_size_check(self):
        if self.target_size is not None and not self.size_timer.isActive():
            self.size_timer.start(0)

    def reconcile_size(self):
        handle = self.windowHandle()
        if not self.isVisible() or self.target_size is None or self.size_retries >= 4:
            return
        if self.size() != self.target_size or (handle and handle.size() != self.target_size):
            # Expose/configure can deliver an old native size *after* a resize
            # requested inside Expose. Reapply outside that event dispatch.
            # Bounded retries avoid fighting persistent compositor constraints.
            self.size_retries += 1
            self.resize(self.target_size)
            if handle and handle.size() != self.target_size:
                handle.resize(self.target_size)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, "size_timer"):
            self.schedule_size_check()

    def event(self, event):
        result = super().event(event)
        # QWidget can receive events during construction, before our fields exist.
        # DeferredDelete may destroy the native QWidget inside super().event().
        if not sip.isdeleted(self) and hasattr(self, "activation_epoch") and self.isVisible():
            if event.type() == QEvent.Type.WindowActivate:
                self.activation_epoch += 1
                self.activation_observed = True
            elif event.type() == QEvent.Type.WindowDeactivate and self.activation_observed:
                self.activation_epoch += 1
                epoch = self.activation_epoch
                # Let Qt finish the activation transition. A reactivation or a
                # hide/reopen in this event-loop turn invalidates the callback.
                QTimer.singleShot(0, lambda: self.close_if_inactive(epoch))
        return result

    def close_if_inactive(self, epoch):
        if (epoch == self.activation_epoch and self.activation_observed
                and self.isVisible() and not self.isActiveWindow()):
            self.activation_observed = False
            self.focusLost.emit()

    def hideEvent(self, event):
        self.stop_animation()
        self.size_timer.stop()
        self.activation_epoch += 1
        self.activation_observed = False
        super().hideEvent(event)

    def keyPressEvent(self, event):
        if event.isAutoRepeat():
            event.accept()
            return
        if event.modifiers() == Qt.KeyboardModifier.NoModifier:
            if event.key() in (Qt.Key.Key_Space, Qt.Key.Key_Escape):
                self.close_callback()
            elif event.key() in (Qt.Key.Key_Left, Qt.Key.Key_Right):
                self.navigate_callback(-1 if event.key() == Qt.Key.Key_Left else 1)
            elif event.key() in (Qt.Key.Key_0, Qt.Key.Key_F):
                self.fit()
        event.accept()

    def closeEvent(self, event):
        event.ignore()
        self.close_callback()
