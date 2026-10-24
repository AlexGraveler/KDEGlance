# SPDX-License-Identifier: GPL-3.0-or-later
"""Local viewer controls, hover fading and compositor-managed window dragging."""
from PyQt6.QtCore import QObject, Qt, QEvent, QPointF, QRectF, QTimer, QPropertyAnimation, pyqtProperty, pyqtSlot
from PyQt6.QtGui import QPainter, QColor, QCursor
from PyQt6.QtWidgets import QWidget, QPushButton, QHBoxLayout, QGraphicsOpacityEffect, QApplication, QToolTip
from PyQt6 import sip
import weakref
from visuals import draw_icon


class ActionButton(QPushButton):
    def __init__(self, action, parent=None):
        super().__init__(parent)
        self.action = action
        self.setFixedSize(30, 32)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        text = {"rotate": "Повернуть на 90° против часовой стрелки", "flip": "Отразить по горизонтали",
                "fullscreen": "На весь экран / обычный размер"}[action]
        self.setAccessibleName(text)
        self.setToolTip(text)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        if getattr(self, "backdrop", False):
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(QColor(10,16,24,150))
            painter.drawRoundedRect(QRectF(self.rect()),5,5)
        edge = min(24, self.width()-6, self.height()-6)
        draw_icon(painter, self.action,
                  QRectF((self.width()-edge)/2, (self.height()-edge)/2, edge, edge))



class ViewerInteractions(QObject):
    def __init__(self, window, controls):
        super().__init__(window)
        self.window_ref = weakref.ref(window)
        self.controls_ref = weakref.ref(controls)
        self._opacity = 1.0
        self.effect = QGraphicsOpacityEffect(controls)
        controls.setGraphicsEffect(self.effect)
        self.fade = QPropertyAnimation(self, b"opacity", self)
        self.fade.setDuration(1000)
        self.idle = QTimer(self)
        self.idle.setSingleShot(True)
        self.idle.timeout.connect(self.fade_out)
        self.leave_timer = QTimer(self)
        self.leave_timer.setSingleShot(True)
        self.leave_timer.timeout.connect(self.left_window)
        self.inside = False
        self.last_mouse = None
        self.press = None
        self.moving = False
        self.drag_requests = 0
        self.start_move = None
        for widget in [window, *window.findChildren(QWidget)]:
            widget.setMouseTracking(True)
            widget.installEventFilter(self)
        self.set_opacity(0)

    @property
    def window(self):
        return self.window_ref()

    @property
    def controls(self):
        return self.controls_ref()

    def system_move(self):
        window = self.window
        return bool(window and window.windowHandle() and window.windowHandle().startSystemMove())

    def get_opacity(self):
        return self._opacity

    def set_opacity(self, value):
        self._opacity = value
        self.effect.setOpacity(value)
        self.controls.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents, value <= .001)
        canvas = self.window.canvas
        canvas.controls_opacity = value
        canvas.update()
        panel = getattr(self.window, "volume_panel", None)
        if panel and panel.isVisible():
            self.window.volume_opacity.setOpacity(value)
        if value <= .001:
            if panel and panel.isVisible():
                panel.hide()
            QToolTip.hideText()

    opacity = pyqtProperty(float, get_opacity, set_opacity)

    def activity(self, point=None):
        point = point or QCursor.pos()
        self.inside = self.window.rect().contains(self.window.mapFromGlobal(point))
        if not self.inside:
            return
        self.fade.stop()
        self.set_opacity(1)
        self.idle.start(3000)

    @pyqtSlot()
    def fade_out(self):
        self.idle.stop()
        if getattr(self.window, "seek_bar", None) and self.window.seek_bar.dragging:
            self.idle.start(3000)
            return
        if getattr(self.window, "volume_slider", None) and self.window.volume_slider.isSliderDown():
            self.idle.start(3000)
            return
        if hasattr(self.window, "volume_fade"):
            self.window.volume_fade.stop()
            self.window.percent_timer.stop()
        QToolTip.hideText()
        self.fade.stop()
        self.fade.setStartValue(self._opacity)
        self.fade.setEndValue(0.0)
        self.fade.start()

    @pyqtSlot()
    def left_window(self):
        if not self.window.isVisible():
            return
        if not self.window.underMouse():
            self.inside = False
            self.fade_out()

    def eventFilter(self, watched, event):
        w = self.window
        if w is None or sip.isdeleted(w) or sip.isdeleted(self.effect):
            return False
        kind = event.type()
        if watched is w and kind == QEvent.Type.Hide:
            self.idle.stop()
            self.fade.stop()
            self.press, self.moving = None, False
            self.set_opacity(0)
        elif watched is w and kind == QEvent.Type.Show:
            self.activity()
        elif kind == QEvent.Type.Enter:
            # Child enter/leave can be generated by fading hit targets under a
            # stationary pointer. Only actual movement restarts the idle clock.
            if watched is w or not self.inside:
                self.activity(event.globalPosition().toPoint() if hasattr(event,"globalPosition") else None)
        elif kind == QEvent.Type.Leave:
            if watched is w:
                self.inside = False
                self.fade_out()
            else:
                self.leave_timer.start(0)
        elif kind in (QEvent.Type.MouseMove, QEvent.Type.MouseButtonPress, QEvent.Type.Wheel):
            point = event.globalPosition().toPoint()
            if kind != QEvent.Type.MouseMove or point != self.last_mouse:
                self.last_mouse = point
                self.activity(point)
        if watched is w and kind == QEvent.Type.Resize and hasattr(w, "photo_controls"):
            self.position_photo_controls()
        if watched is w.canvas:
            if kind == QEvent.Type.MouseButtonPress and event.button() == Qt.MouseButton.LeftButton:
                self.press, self.moving = QPointF(event.position()), False
            elif kind == QEvent.Type.MouseMove and self.press is not None and event.buttons() & Qt.MouseButton.LeftButton:
                if self.moving:
                    return True
                distance = (event.position()-self.press).manhattanLength()
                if distance >= QApplication.startDragDistance() and not w.isFullScreen():
                    if getattr(w.canvas, "zoom", 1) <= 1 or event.modifiers() & Qt.KeyboardModifier.AltModifier:
                        self.moving = True
                        self.drag_requests += 1
                        w.canvas.dragged = True
                        (self.start_move or self.system_move)()
                        return True
            elif kind == QEvent.Type.MouseButtonRelease:
                self.press = None
                if self.moving:
                    self.moving = False
                    if hasattr(w.canvas, "reset_gesture"):
                        w.canvas.reset_gesture()
                    else:
                        w.canvas.press_position = None
                    return True
        return False

    def position_photo_controls(self):
        self.controls.setGeometry(max(0,self.window.width()-80),max(0,self.window.height()-44),76,40)
        self.controls.raise_()


def install_interactions(window, photo=False):
    if photo:
        panel = QWidget(window.canvas)
        panel.setStyleSheet("QWidget { background: transparent; }")
        layout = QHBoxLayout(panel)
        layout.setContentsMargins(3,2,3,2)
        layout.setSpacing(2)
        window.rotate_button = ActionButton("rotate", panel)
        window.flip_button = ActionButton("flip", panel)
        window.rotate_button.backdrop = window.flip_button.backdrop = True
        window.rotate_button.clicked.connect(window.rotate_image)
        window.flip_button.clicked.connect(window.flip_image)
        layout.addWidget(window.rotate_button)
        layout.addWidget(window.flip_button)
        window.photo_controls = panel
        controls = panel
    else:
        controls = window.controls
    window.interactions = ViewerInteractions(window, controls)
    if photo:
        window.interactions.position_photo_controls()
