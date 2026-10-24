#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""KDEGlance resident viewer. Run via the user service, without arguments."""
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import signal
import sys
import time
from collections import OrderedDict
from PyQt6.QtCore import QObject, QThreadPool, QTimer, pyqtSlot, pyqtClassInfo
from PyQt6.QtDBus import QDBusConnection
from PyQt6.QtGui import QImageReader, QCursor
from PyQt6.QtWidgets import QApplication
from core import VERSION, BUS, OBJECT, INTERFACE, local_path, media_kind
from loader import LoadJob, ImageCache, HEIF_EXTENSIONS
from window import PreviewWindow

LOG = logging.getLogger("kdeglance2")


@pyqtClassInfo("D-Bus Interface", INTERFACE)
class Controller(QObject):
    def __init__(self, app):
        super().__init__()
        self.app = app
        self.pool = QThreadPool(self)
        self.pool.setMaxThreadCount(2)
        self.jobs = {}
        self.cache = ImageCache()
        self.prefetch_job = None
        self.prefetch_queue = []
        self.waiting_prefetch = None
        self.navigation_direction = 1
        self.last_cache_hit = False
        self.cache_idle_timer = QTimer(self)
        self.cache_idle_timer.setSingleShot(True)
        self.cache_idle_timer.timeout.connect(self.cache.clear)
        self.generation = 0
        self.path, self.error, self.phase = "", "", "idle"
        self.selected, self.playlist = [], []
        self.seen = OrderedDict()
        self.last_toggle = 0.0
        self.last_toggle_path = ""
        self.screen_name = ""
        self.activation_token_received = False
        self.pending_fit = None
        self.audio = None
        self.audio_order = None
        self.audio_pending_navigation = []
        self.image_selection = None
        self.image_last_displayed = ""
        self.image_pending_navigation = []
        self.video = None
        self.video_order = None
        self.video_pending_navigation = []
        self.mode = "image"
        self.window = PreviewWindow(self.close_view, self.navigate)
        self.window.outputChanged.connect(self.output_changed)
        self.window.exposed.connect(self.fit_after_expose)
        self.window.focusLost.connect(lambda: self.close_view("focus-loss"))
        self.window.animationError.connect(self.animation_failed)
        self.app.screenRemoved.connect(lambda _: QTimer.singleShot(0, self.screen_changed))
        self.app.primaryScreenChanged.connect(lambda _: self.screen_changed())
        for screen in self.app.screens():
            self.attach_screen(screen)
        self.app.screenAdded.connect(self.attach_screen)
        self.app.lastWindowClosed.connect(lambda: None)

    def attach_screen(self, screen):
        screen.availableGeometryChanged.connect(lambda _: self.screen_changed())

    def animation_failed(self, error):
        self.phase, self.error = 'error', error
        LOG.warning('animation decode error path=%r error=%s', self.path, error)

    def output_changed(self, screen):
        if screen is not None:
            self.screen_name = screen.name()
            self.screen_changed()

    def screen(self):
        for screen in self.app.screens():
            if screen.name() == self.screen_name:
                return screen
        return self.app.screenAt(QCursor.pos()) or self.window.screen() or self.app.primaryScreen()

    def screen_changed(self):
        if self.mode == "video" and self.video and self.video.isVisible():
            self.video.resize_video(self.screen())
            return
        if self.mode == "audio" and self.audio and self.audio.isVisible():
            self.audio.resize_audio(self.screen())
            return
        if self.window.isVisible() and self.phase == "ready":
            self.window.resize_to_image(
                self.window.canvas.image if not self.window.canvas.image.isNull()
                else self.placeholder(), self.screen())

    def fit_after_expose(self):
        handle = self.window.windowHandle()
        if (self.pending_fit == self.generation and self.phase == "ready" and
                self.window.frames and handle and handle.isExposed()):
            self.pending_fit = None
            self.window.resize_to_image(self.window.frames[0][0], self.screen())

    @staticmethod
    def placeholder():
        from PyQt6.QtGui import QImage
        return QImage(520, 300, QImage.Format.Format_RGB32)

    def state(self):
        if self.mode == "video" and self.video:
            return {"version": VERSION, "pid": os.getpid(), "module": "video",
                    "path": self.path, "visible": self.video.isVisible(),
                    "generation": self.generation, "platform": self.app.platformName(),
                    "video_initialized": True, "video": self.video.snapshot(),
                    "phase": self.video.phase, "error": self.video.error,
                    "window_size": [self.video.width(), self.video.height()],
                    "fullscreen": self.video.isFullScreen(),
                    "active": self.video.isActiveWindow(),
                    "activation_observed": self.video.activation_observed,
                    "dolphin_order_ready": bool(self.video_order and self.video_order.paths),
                    "dolphin_selection_synced": bool(self.video_order and self.video_order.selected == self.path),
                    "window_count": sum(w.isVisible() for w in self.app.topLevelWidgets()
                                        if isinstance(w, PreviewWindow))}
        if self.mode == "audio" and self.audio:
            return {"version": VERSION, "pid": os.getpid(), "module": "audio",
                    "path": self.path, "visible": self.audio.isVisible(),
                    "generation": self.generation, "platform": self.app.platformName(),
                    "audio_initialized": True, "audio": self.audio.snapshot(),
                    "phase": self.audio.phase, "error": self.audio.error,
                    "window_size": [self.audio.width(), self.audio.height()],
                    "fullscreen": self.audio.isFullScreen(),
                    "active": self.audio.isActiveWindow(),
                    "activation_observed": self.audio.activation_observed,
                    "dolphin_order_ready": bool(self.audio_order and self.audio_order.paths),
                    "dolphin_selection_synced": bool(self.audio_order and self.audio_order.selected == self.path),
                    "window_count": sum(w.isVisible() for w in self.app.topLevelWidgets()
                                        if isinstance(w, PreviewWindow))}
        return {"version": VERSION, "pid": os.getpid(), "visible": self.window.isVisible(),
                "module": "image", "audio_initialized": self.audio is not None,
                "dolphin_selection_synced": bool(self.image_selection and
                    self.image_selection.selected == self.image_last_displayed),
                "audio_playing": bool(self.audio and self.audio.snapshot()["playing"]),
                "path": self.path, "phase": self.phase, "error": self.error,
                "generation": self.generation, "playlist": self.playlist,
                "frame_count": len(self.window.frames), "frame_index": self.window.frame_index,
                "animation": ({**self.window.animation.snapshot(),
                    "frame_count": self.window.animation_frame_count,
                    "frame_index": self.window.animation_index,
                    "displayed_frames": self.window.animation_displayed} if self.window.animation else None),
                "zoom": self.window.canvas.zoom, "platform": self.app.platformName(),
                "screen": self.window.screen().name(),
                "window_count": sum(type(w) is PreviewWindow for w in self.app.topLevelWidgets()),
                "fullscreen": self.window.isFullScreen(),
                "window_size": [self.window.width(), self.window.height()],
                "canvas_size": [self.window.canvas.width(), self.window.canvas.height()],
                "rendered_size": [self.window.canvas.image_rect().width(),
                                  self.window.canvas.image_rect().height()],
                "native_window_size": ([self.window.windowHandle().width(), self.window.windowHandle().height()]
                                       if self.window.windowHandle() else None),
                "native_activation": self.window.native is not None,
                "active": self.window.isActiveWindow(),
                "activation_observed": self.window.activation_observed,
                "cache_hit": self.last_cache_hit,
                "heif_cache_bytes": self.cache.bytes,
                "loading_placeholder": self.phase == "loading" and bool(self.window.canvas.message),
                "retained_frame": self.phase == "loading" and bool(self.window.frames),
                "activation_token_received": self.activation_token_received}

    def request(self, text, toggle):
        payload = json.loads(text)
        if not isinstance(payload, dict):
            raise ValueError("Ожидается JSON-объект")
        values = payload.get("paths", [])
        if not isinstance(values, list) or not 1 <= len(values) <= 128 or not all(isinstance(p, str) for p in values):
            raise ValueError("paths должен содержать от 1 до 128 строк")
        paths = list(dict.fromkeys(local_path(p) for p in values))
        ident = payload.get("request_id", "")
        if not isinstance(ident, str) or len(ident) > 128:
            raise ValueError("Некорректный request_id")
        if ident and ident in self.seen:
            return {"ok": True, "ignored": "duplicate", **self.state()}
        if ident:
            self.seen[ident] = None
            if len(self.seen) > 256:
                self.seen.popitem(last=False)
        now = time.monotonic()
        if toggle:
            gap = now - self.last_toggle
            same = self.last_toggle_path == paths[0]
            self.last_toggle, self.last_toggle_path = now, paths[0]
            # Sliding guard suppresses a burst; local QKeyEvent autorepeat is ignored.
            if same and gap < 0.25:
                return {"ok": True, "ignored": "burst", **self.state()}
            current = self.audio if self.mode == "audio" else self.window
            if self.mode == "video":
                current = self.video
            if current and current.isVisible() and self.path == paths[0]:
                self.close_view()
                return {"ok": True, **self.state()}
        self.screen_name = str(payload.get("screen_name", ""))[:256]
        token = payload.get("activation_token", "")
        if not isinstance(token, str) or len(token) > 8192 or "\0" in token:
            raise ValueError("Некорректный activation_token")
        self.activation_token_received = bool(token)
        LOG.info("open request activation_token_received=%s screen=%r", bool(token), self.screen_name)
        self.selected = paths
        if media_kind(paths[0]) == "video":
            self.open_video(paths[0], token)
            return {"ok": True, **self.state()}
        if media_kind(paths[0]) == "audio":
            self.open_audio(paths[0], token)
            return {"ok": True, **self.state()}
        if self.mode in ("audio", "video"):
            self.close_view()
        self.finish_image_selection()
        self.image_last_displayed = ""
        self.mode = "image"
        self.load(paths[0], token, present=True)
        if self.app.platformName() == "wayland":
            from dolphin_order import DolphinOrder
            selection = DolphinOrder(paths[0], self, media="all")
            self.image_selection = selection
            selection.ready.connect(lambda: self.image_order_ready(selection))
            selection.failed.connect(lambda error: LOG.warning("Dolphin image selection: %s", error))
        return {"ok": True, **self.state()}

    def finish_image_selection(self):
        selection, self.image_selection = self.image_selection, None
        if selection:
            selection.idle.connect(selection.deleteLater)
            selection.finish(self.image_last_displayed or selection.path)

    def image_order_ready(self, order):
        if order is not self.image_selection or not self.window.isVisible():
            return
        self.playlist = order.paths
        pending, self.image_pending_navigation = self.image_pending_navigation, []
        for direction in pending:
            self.navigate(direction)

    def active_order(self):
        return self.video_order if self.mode == "video" else self.audio_order if self.mode == "audio" else self.image_selection

    def show_navigation_target(self, path, order=None):
        mode = media_kind(path)
        if mode != self.mode:
            # Keep one folder snapshot/selection worker throughout mixed navigation.
            self.audio_order = self.video_order = self.image_selection = None
            self.selected = [path]
            if mode == "video":
                self.open_video(path, "", order=order)
            elif mode == "audio":
                self.open_audio(path, "", order=order)
            else:
                self.close_view()
                self.mode = "image"
                self.image_last_displayed = ""
                self.image_selection = order
                self.load(path, present=True)
        else:
            self.path = path
            if mode == "video":
                self.video.open_video(path)
            elif mode == "audio":
                self.audio.open_audio(path)
            else:
                self.load(path)
        if mode != "image" and order:
            order.select(path)
        self.last_toggle, self.last_toggle_path = time.monotonic(), path

    def open_video(self, path, token, order=None):
        self.close_view()
        if self.video is None:
            from video import VideoWindow
            self.video = VideoWindow(self.close_view, self.navigate)
            self.video.focusLost.connect(lambda: self.close_view("focus-loss"))
            self.video.outputChanged.connect(self.output_changed)
        self.mode, self.path, self.phase, self.error = "video", path, "loading", ""
        self.last_toggle, self.last_toggle_path = time.monotonic(), path
        self.video.open_video(path)
        self.video.present(self.screen(), token)
        if order is not None:
            self.video_order = order
        elif self.app.platformName() == "wayland":
            from dolphin_order import DolphinOrder
            order = DolphinOrder(path, self, media="all")
            self.video_order = order
            order.ready.connect(lambda: self.video_order_ready(order))
            order.failed.connect(lambda error: self.video_order_failed(order, error))

    def video_order_ready(self, order):
        if order is not self.video_order or not self.video.isVisible():
            return
        pending, self.video_pending_navigation = self.video_pending_navigation, []
        for direction in pending:
            self.navigate(direction)

    def video_order_failed(self, order, error):
        if order is not self.video_order:
            return
        LOG.warning("Dolphin video order/selection: %s", error)
        if self.video_pending_navigation or order.paths:
            from PyQt6.QtWidgets import QToolTip
            QToolTip.showText(QCursor.pos(), error)
        self.video_pending_navigation = []

    def open_audio(self, path, token, order=None):
        self.close_view()
        if self.audio is None:
            from audio import AudioWindow
            self.audio = AudioWindow(self.close_view, self.navigate)
            self.audio.focusLost.connect(lambda: self.close_view("focus-loss"))
            self.audio.outputChanged.connect(self.output_changed)
        self.mode, self.path, self.phase, self.error = "audio", path, "loading", ""
        self.last_toggle, self.last_toggle_path = time.monotonic(), path
        self.audio.open_audio(path)
        self.audio.present(self.screen(), token)
        if order is not None:
            self.audio_order = order
        elif self.app.platformName() == "wayland":
            from dolphin_order import DolphinOrder
            order = DolphinOrder(path,self,media="all")
            self.audio_order = order
            order.ready.connect(lambda: self.audio_order_ready(order))
            order.failed.connect(lambda error: self.audio_order_failed(order,error))


    def audio_order_ready(self, order):
        if order is not self.audio_order or not self.audio.isVisible():
            return
        pending, self.audio_pending_navigation = self.audio_pending_navigation, []
        for direction in pending:
            self.navigate(direction)

    def audio_order_failed(self, order, error):
        if order is not self.audio_order:
            return
        LOG.warning("Dolphin order/selection: %s",error)
        if self.audio_pending_navigation or order.paths:
            from PyQt6.QtWidgets import QToolTip
            QToolTip.showText(QCursor.pos(),error)
        self.audio_pending_navigation = []

    def load(self, path, token="", present=False):
        self.cache_idle_timer.stop()
        self.prefetch_queue = []
        self.waiting_prefetch = None
        self.generation += 1
        for job in self.jobs.values():
            job.cancelled.set()
        self.path, self.error, self.phase = path, "", "loading"
        self.pending_fit = None
        self.playlist = list(self.selected) if len(self.selected) > 1 else [path]
        if self.window.isVisible() and self.window.frames:
            # Keep the previous image until the next complete frame is ready.
            # Even an 8 ms cache hit otherwise flashes the loading placeholder.
            self.window.stop_animation()
            self.window.canvas.reset_gesture()
        else:
            self.window.clear_content("Загрузка…")
        self.window.setWindowTitle(Path(path).name + " — KDEGlance")
        if present:
            self.window.present(self.screen(), token)
        if (self.prefetch_job and self.prefetch_job.path == path
                and not self.prefetch_job.cancelled.is_set()):
            # Promote the in-flight decode instead of decoding the same HEIC twice.
            self.waiting_prefetch = self.generation
            return
        job = LoadJob(self.generation, path, list(self.selected), self.cache, mixed=True)
        self.jobs[self.generation] = job
        job.signals.done.connect(self.loaded)
        self.pool.start(job, 1)
        LOG.info("load generation=%d path=%r", self.generation, path)

    @pyqtSlot(int, object)
    def loaded(self, generation, result):
        self.jobs.pop(generation, None)
        if generation != self.generation or not self.window.isVisible() or result.get("cancelled"):
            return
        self.playlist = result["playlist"]
        if self.image_selection and getattr(self.image_selection, "paths", None):
            self.playlist = self.image_selection.paths
        self.error = result.get("error", "")
        if self.error:
            self.phase = "error"
            self.window.clear_content(self.error)
            LOG.warning("decode error path=%r error=%s", self.path, self.error)
        else:
            self.phase = "ready"
            self.last_cache_hit = result.get("cache_hit", False)
            self.window.set_frames(result["frames"], result["loops"],
                                   result.get('animation_path'), result.get('animation_frame_count', 0))
            self.image_last_displayed = self.path
            if self.image_selection:
                self.image_selection.select(self.path)
            self.pending_fit = self.generation
            self.fit_after_expose()
            self.plan_prefetch()

    def plan_prefetch(self):
        if not self.playlist or self.path not in self.playlist:
            return
        index = self.playlist.index(self.path)
        candidates = [self.playlist[(index+direction) % len(self.playlist)]
                      for direction in (self.navigation_direction, -self.navigation_direction)]
        self.prefetch_queue = list(dict.fromkeys(
            path for path in candidates if path != self.path
            and Path(path).suffix.lower() in HEIF_EXTENSIONS))
        self.start_prefetch()

    def start_prefetch(self):
        if self.prefetch_job or not self.prefetch_queue or not self.window.isVisible():
            return
        path = self.prefetch_queue.pop(0)
        job = LoadJob(-1, path, [], self.cache, mixed=True)
        self.prefetch_job = job
        job.signals.done.connect(self.prefetched)
        # At most one background job, lower priority than ordinary image loads.
        self.pool.start(job, -1)

    @pyqtSlot(int, object)
    def prefetched(self, _generation, result):
        job = self.prefetch_job
        self.prefetch_job = None
        waiting, self.waiting_prefetch = self.waiting_prefetch, None
        if (job and waiting == self.generation and self.path == job.path
                and self.window.isVisible() and not job.cancelled.is_set()):
            if len(self.selected) > 1:
                result = {**result, "playlist": list(self.selected)}
            self.loaded(waiting, result)
        self.start_prefetch()

    def close_view(self, reason="request"):
        self.image_pending_navigation = []
        video_order, self.video_order = self.video_order, None
        self.video_pending_navigation = []
        if video_order:
            video_order.idle.connect(video_order.deleteLater)
            video_order.finish(self.path)
        if self.video:
            self.video.stop_video()
            self.video.hide()
        self.finish_image_selection()
        order, self.audio_order = self.audio_order, None
        self.audio_pending_navigation = []
        if order:
            order.idle.connect(order.deleteLater)
            order.finish(self.path)  # Commit the last accepted track even after Space.
        if self.audio:
            self.audio.stop_audio()
            self.audio.hide()
        self.prefetch_queue = []
        self.waiting_prefetch = None
        if self.prefetch_job:
            self.prefetch_job.cancelled.set()
        self.cache_idle_timer.start(60_000)
        self.generation += 1
        for job in self.jobs.values():
            job.cancelled.set()
        self.window.hide()
        self.window.clear_content("")
        self.phase = "idle"
        self.last_toggle, self.last_toggle_path = time.monotonic(), self.path
        LOG.info("closed reason=%s", reason)

    def navigate(self, direction):
        current = self.video if self.mode == "video" else self.audio if self.mode == "audio" else self.window
        if not current or not current.isVisible():
            return
        order = self.active_order()
        paths = getattr(order, "paths", [])
        if paths:
            playlist = [p for p in paths if Path(p).is_file()]
            if self.path in playlist and len(playlist) > 1:
                path = playlist[(playlist.index(self.path)+direction) % len(playlist)]
                self.navigation_direction = direction
                self.show_navigation_target(path, order)
            return
        if order and getattr(order, "process", None) is not None:
            pending = self.video_pending_navigation if self.mode == "video" else self.audio_pending_navigation if self.mode == "audio" else self.image_pending_navigation
            pending.append(direction)
            return
        if self.mode != "image":
            from PyQt6.QtWidgets import QToolTip
            QToolTip.showText(QCursor.pos(), order.error if order else
                "Для навигации откройте файл из папки Dolphin через Space")
            return
        if self.phase == "loading" or not self.playlist or self.path not in self.playlist:
            return
        path = self.playlist[(self.playlist.index(self.path)+direction) % len(self.playlist)]
        self.navigation_direction = direction
        self.show_navigation_target(path, order)

    def invoke(self, text, toggle):
        try:
            result = self.request(text, toggle)
        except (ValueError, TypeError, OSError) as error:
            LOG.warning("request rejected: %s", error)
            result = {"ok": False, "error": str(error)}
        return json.dumps(result, ensure_ascii=True)

    @pyqtSlot(str, result=str)
    def Open(self, request):
        return self.invoke(request, False)

    @pyqtSlot(str, result=str)
    def Toggle(self, request):
        return self.invoke(request, True)

    @pyqtSlot(result=str)
    def Close(self):
        self.close_view()
        return json.dumps({"ok": True, **self.state()})

    @pyqtSlot(result=str)
    def GetState(self):
        return json.dumps(self.state(), ensure_ascii=True)


def main():
    app = QApplication(["kdeglance2"])
    app.setApplicationName("kdeglance2")
    app.setDesktopFileName("io.github.KDEGlance2")
    app.setQuitOnLastWindowClosed(False)
    bus = QDBusConnection.sessionBus()
    if not bus.isConnected():
        raise RuntimeError("Пользовательская D-Bus-сессия недоступна")
    if not bus.registerService(BUS):
        if bus.interface().isServiceRegistered(BUS).value():
            print("KDEGlance уже работает", file=sys.stderr)
            return 0
        raise RuntimeError(bus.lastError().message())
    state_dir = Path(os.environ.get("XDG_STATE_HOME") or Path.home()/".local/state") / "kdeglance2"
    state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    handler = RotatingFileHandler(state_dir / "kdeglance2.log", maxBytes=1_000_000, backupCount=3, encoding="utf-8")
    logging.basicConfig(level=logging.INFO, handlers=[handler, logging.StreamHandler()],
                        format="%(asctime)s %(levelname)s %(message)s")
    QImageReader.setAllocationLimit(256)
    controller = Controller(app)
    app.aboutToQuit.connect(controller.close_view)
    if not bus.registerObject(OBJECT, controller, QDBusConnection.RegisterOption.ExportAllSlots):
        raise RuntimeError(bus.lastError().message())
    signal.signal(signal.SIGTERM, lambda *_: app.quit())
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    heartbeat = QTimer()
    heartbeat.timeout.connect(lambda: None)
    heartbeat.start(250)
    LOG.info("resident started version=%s platform=%s", VERSION, app.platformName())
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
