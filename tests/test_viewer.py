# SPDX-License-Identifier: GPL-3.0-or-later
#!/usr/bin/env python3
"""Run inside dbus-run-session with QT_QPA_PLATFORM=offscreen."""
import ast
import concurrent.futures
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
import uuid
ROOT = Path(__file__).resolve().parent.parent/"kdeglance"
sys.path.insert(0, str(ROOT))
from PIL import Image
from PyQt6.QtCore import Qt, QPointF, QPoint, QRect, QRectF, QEvent, QMimeDatabase
from PyQt6.QtGui import QImage, QKeyEvent, QWheelEvent, QHideEvent, QMouseEvent, QColor
from PyQt6.QtTest import QTest
from PyQt6.QtWidgets import QApplication
from PyQt6.QtDBus import QDBusConnection
from client import call
from core import local_path
from loader import LoadJob
from window import Canvas, PreviewWindow



class ViewerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="viewer-", dir=os.environ["KDEGLANCE_TEST_ROOT"])
        cls.home = Path(cls.temp.name)
        cls.fixtures = cls.home/"images"
        cls.fixtures.mkdir()
        cls.first = cls.fixtures/"01 — пробел %20 [скобки] ' \" $.png"
        Image.new("RGB", (640, 320), "red").save(cls.first)
        cls.second = cls.fixtures/"02 — другой.png"
        Image.new("RGB", (320, 640), "blue").save(cls.second)
        cls.gif = cls.fixtures/"03.gif"
        a, b = Image.new("RGB", (80, 40), "red"), Image.new("RGB", (80, 40), "blue")
        a.save(cls.gif, save_all=True, append_images=[b], duration=[80, 120], loop=0)
        cls.rotated = cls.fixtures/"04.jpg"
        exif = Image.Exif()
        exif[274] = 6
        Image.new("RGB", (120, 60), "green").save(cls.rotated, exif=exif)
        cls.bad = cls.fixtures/"05-broken.png"
        cls.bad.write_text("not an image")
        cls.text = cls.fixtures/"06.txt"
        cls.text.write_text("not supported")
        cls.env = {**os.environ, "HOME": str(cls.home), "XDG_DATA_HOME": str(cls.home/"data"),
                   "XDG_CONFIG_HOME": str(cls.home/"config"), "XDG_STATE_HOME": str(cls.home/"state"),
                   "QT_QPA_PLATFORM": "offscreen"}
        audio_config = cls.home/"config/kdeglance2/audio.ini"
        audio_config.parent.mkdir(parents=True)
        audio_config.write_text("[General]\nvolume=0\n")
        cls.log = (cls.home/"daemon-output.log").open("w+")
        cls.daemon = subprocess.Popen([sys.executable, "-B", str(ROOT/"daemon.py")],
                                       env=cls.env, stdout=cls.log, stderr=cls.log)
        for _ in range(100):
            try:
                if not QDBusConnection.sessionBus().interface().isServiceRegistered("io.github.KDEGlance2").value():
                    time.sleep(0.05)
                    continue
                cls.pid = call("GetState")["pid"]
                if cls.pid != cls.daemon.pid:
                    raise RuntimeError("Unexpected daemon in private test session")
                break
            except (RuntimeError, IndexError):
                if cls.daemon.poll() is not None:
                    cls.log.seek(0)
                    raise RuntimeError(cls.log.read())
                time.sleep(0.05)
        else:
            raise RuntimeError("daemon timeout")

    @classmethod
    def tearDownClass(cls):
        cls.daemon.terminate()
        cls.daemon.wait(timeout=10)
        cls.log.close()
        cls.temp.cleanup()

    def setUp(self):
        call("Close")
        time.sleep(0.27)

    def request(self, method, path, **extra):
        return call(method, {"paths": [str(path)], "request_id": str(uuid.uuid4()), **extra})

    def ready(self):
        for _ in range(150):
            state = call("GetState")
            if state["phase"] != "loading":
                return state
            time.sleep(0.02)
        self.fail("decode timeout")

    def test_01_path_roundtrip_and_lifecycle(self):
        self.assertEqual(local_path(str(self.first)), str(self.first))
        self.assertEqual(local_path(self.first.as_uri()), str(self.first))
        self.request("Open", self.first)
        state = self.ready()
        self.assertEqual(state["path"], str(self.first))
        self.assertEqual(state["phase"], "ready")
        self.assertEqual(state["window_count"], 1)
        self.assertFalse(state["fullscreen"])
        self.assertEqual(state["pid"], self.pid)
        self.assertLessEqual(state["window_size"][0], 800)
        call("Close")
        self.assertFalse(call("GetState")["visible"])
        self.assertEqual(call("GetState")["pid"], self.pid)

    def test_02_single_instance(self):
        result = subprocess.run([sys.executable, "-B", str(ROOT/"daemon.py")], env=self.env,
                                capture_output=True, text=True, timeout=8)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(call("GetState")["pid"], self.pid)

    def test_03_toggle_and_burst(self):
        self.request("Toggle", self.first)
        self.assertTrue(self.ready()["visible"])
        time.sleep(0.27)
        self.request("Toggle", self.first)
        self.assertFalse(call("GetState")["visible"])
        for _ in range(10):
            self.request("Toggle", self.first)
        self.assertFalse(call("GetState")["visible"])

    def test_04_duplicates_and_stale_results(self):
        ident = str(uuid.uuid4())
        call("Toggle", {"paths": [str(self.first)], "request_id": ident})
        self.assertEqual(call("Toggle", {"paths": [str(self.first)], "request_id": ident})["ignored"], "duplicate")
        for i in range(12):
            self.request("Open", self.first if i % 2 else self.second)
        self.request("Open", self.second)
        state = self.ready()
        self.assertEqual(state["path"], str(self.second))
        self.assertEqual(state["phase"], "ready")
        self.assertEqual(state["window_count"], 1)

    def test_05_failures_not_substituted(self):
        for path in (self.bad, self.text, self.fixtures/"missing.png", self.fixtures):
            self.request("Open", path)
            state = self.ready()
            self.assertEqual(state["phase"], "error")
            self.assertEqual(state["path"], str(path))
            self.assertEqual(state["frame_count"], 0)
        self.assertFalse(call("Open", {"paths": ["smb://server/file.png"]})["ok"])

    def test_06_animation_and_orientation(self):
        self.request("Open", self.gif)
        state = self.ready()
        self.assertEqual(state["frame_count"], 2)
        indices = set()
        for _ in range(15):
            indices.add(call("GetState")["frame_index"])
            time.sleep(0.04)
        self.assertEqual(indices, {0, 1})
        job = LoadJob(1, str(self.rotated), [str(self.rotated)])
        frame = job.decode()["frames"][0][0]
        self.assertEqual((frame.width(), frame.height()), (60, 120))

    def test_07_client_and_parallel_calls(self):
        def launch(_):
            return subprocess.run([sys.executable, "-B", str(ROOT/"client.py"), "open", "--", str(self.first)],
                                  env=self.env, capture_output=True, text=True, timeout=12)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for result in pool.map(launch, range(8)):
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout)["path"], str(self.first))
        self.assertEqual(self.ready()["pid"], self.pid)

    def test_08_canvas_and_keyboard(self):
        canvas = Canvas()
        canvas.resize(400, 200)
        canvas.image = QImage(800, 400, QImage.Format.Format_RGB32)
        self.assertEqual(canvas.fit_scale(), 0.5)
        mouse = QPointF(300, 100)
        old_rect = canvas.image_rect()
        old_fraction = (mouse.x()-old_rect.x())/old_rect.width()
        wheel = QWheelEvent(mouse, mouse, QPoint(), QPoint(0, 120),
                            Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier,
                            Qt.ScrollPhase.NoScrollPhase, False)
        app.sendEvent(canvas, wheel)
        new_rect = canvas.image_rect()
        self.assertAlmostEqual((mouse.x()-new_rect.x())/new_rect.width(), old_fraction)
        self.assertGreater(canvas.zoom, 1)
        canvas.zoom = 3
        canvas.pan = QPointF(10000, -10000)
        canvas.clamp_pan()
        self.assertEqual(canvas.pan, QPointF(400, -200))
        closed, moved = [], []
        window = PreviewWindow(lambda: closed.append(True), lambda d: moved.append(d))
        window.resize(420, 300)
        window.show()
        app.processEvents()
        repeat = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_Space, Qt.KeyboardModifier.NoModifier, " ", True)
        app.sendEvent(window, repeat)
        self.assertEqual(closed, [])
        QTest.keyClick(window, Qt.Key.Key_Escape)
        QTest.keyClick(window, Qt.Key.Key_Space)
        QTest.keyClick(window, Qt.Key.Key_Right)
        self.assertEqual(len(closed), 2)
        self.assertEqual(moved, [1])
        self.assertTrue(window.mask().isEmpty())
        window.hide()

    def test_09_decode_limits_and_formats(self):
        for ext in ("png", "bmp", "webp", "jpeg"):
            path = self.home/("format."+ext)
            Image.new("RGB", (32, 16), "cyan").save(path)
            self.assertEqual(len(LoadJob(1, str(path), [str(path)]).decode()["frames"]), 1)
        big = self.home/"big.jpg"
        Image.new("RGB", (6000, 4500), "yellow").save(big)
        self.request("Open", big)
        # GetState must remain available while decoder is working.
        started = time.monotonic()
        self.assertEqual(call("GetState")["pid"], self.pid)
        self.assertLess(time.monotonic()-started, 1.0)
        self.assertEqual(self.ready()["phase"], "ready")
        result = LoadJob(1, str(big), [str(big)]).decode()
        self.assertLessEqual(max(result["decoded_size"]), 4096)

    def test_10_install_update_uninstall(self):
        isolated = self.home/"install-test"
        isolated.mkdir()
        env = {**self.env, "HOME": str(isolated), "XDG_DATA_HOME": str(isolated/"data"),
               "XDG_CONFIG_HOME": str(isolated/"config"), "XDG_STATE_HOME": str(isolated/"state")}
        probe = isolated/"data/applications/org.kde.dolphin.desktop"
        probe.parent.mkdir(parents=True)
        original = b"[Desktop Entry]\nType=Application\nName=Dolphin\nExec=dolphin %U\nDBusActivatable=true\n"
        probe.write_bytes(original)
        xml = isolated/"data/kxmlgui5/dolphin/dolphinui.rc"
        xml.parent.mkdir(parents=True)
        xml.write_text('<gui><Action name="servicemenu_kdeglance-space-probe.desktop::LogSelection" shortcut="Space"/></gui>')
        xml_hash = hashlib.sha256(xml.read_bytes()).digest()
        for command in ("install", "update"):
            result = subprocess.run([sys.executable, "-B", str(ROOT/"manage.py"), command, "--isolated"],
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(hashlib.sha256(xml.read_bytes()).digest(), xml_hash)
        desktop = isolated/"data/applications/io.github.KDEGlance2.desktop"
        result = subprocess.run(["desktop-file-validate", str(desktop)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        result = subprocess.run([sys.executable, "-B", str(ROOT/"manage.py"), "uninstall", "--isolated", "--purge"],
                                env=env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(probe.read_bytes(), original)
        self.assertEqual(hashlib.sha256(xml.read_bytes()).digest(), xml_hash)
        self.assertFalse((isolated/"data/kdeglance2").exists())
        self.assertFalse((isolated/"state/kdeglance2").exists())

    def test_12_integration_rollback_and_safe_uninstall(self):
        home = self.home/"rollback-test"
        env = {**self.env, "HOME": str(home), "XDG_DATA_HOME": str(home/"data"),
               "XDG_CONFIG_HOME": str(home/"config"), "XDG_STATE_HOME": str(home/"state")}
        probe = home/"data/kio/servicemenus/kdeglance-space-probe.desktop"
        probe.parent.mkdir(parents=True)
        def manage(*args):
            return subprocess.run([sys.executable, "-B", str(ROOT/"manage.py"), *args],
                                  env=env, capture_output=True, text=True, timeout=15)
        self.assertEqual(manage("install", "--isolated").returncode, 0)
        self.assertEqual(manage("rollback-integration").returncode, 0)
        self.assertFalse(probe.exists())
        unit = home/"config/systemd/user/kdeglance2.service"
        original = unit.read_text()
        unit.write_text(original + "# user edit\n")
        self.assertNotEqual(manage("uninstall", "--isolated").returncode, 0)
        self.assertTrue((home/"data/kdeglance2/current/daemon.py").is_file())
        unit.write_text(original)
        self.assertEqual(manage("uninstall", "--isolated", "--purge").returncode, 0)
        self.assertFalse(probe.exists())

    def test_11_no_legacy_mechanisms(self):
        for filename in ("daemon.py", "client.py", "loader.py", "window.py"):
            tree = ast.parse((ROOT/filename).read_text())
            calls = {n.func.attr for n in ast.walk(tree) if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
            self.assertFalse(calls & {"clipboard", "showFullScreen", "grabKeyboard", "setKeyboardGrabEnabled"})

    def test_13_activation_lifecycle(self):
        # Deterministic Qt-event tests; real compositor delivery is checked
        # separately in the user's Wayland session.
        class EventWindow(PreviewWindow):
            visible = True
            active = False

            def isVisible(self):
                return self.visible

            def isActiveWindow(self):
                return self.active

        window = EventWindow(lambda: None, lambda _: None)
        closed = []
        window.focusLost.connect(lambda: closed.append(True))

        def send(kind):
            QApplication.sendEvent(window, QEvent(kind))

        def drain():
            app.processEvents()

        # No timer/grace-period guess: an arbitrarily delayed first activation
        # must not close the preview.
        send(QEvent.Type.WindowDeactivate)
        QTest.qWait(30)
        self.assertFalse(closed)
        self.assertFalse(window.activation_observed)
        window.active = True
        send(QEvent.Type.WindowActivate)
        window.clear_content("loading")
        image = QImage(100, 100, QImage.Format.Format_RGB32)
        window.set_frames([(image, 100)], -1)
        window.resize(500, 400)
        send(QEvent.Type.FocusOut)
        window.canvas.setFocus()
        drain()
        self.assertTrue(window.activation_observed)
        self.assertFalse(closed)

        # A transient deactivation followed by reactivation is cancelled.
        window.active = False
        send(QEvent.Type.WindowDeactivate)
        window.active = True
        send(QEvent.Type.WindowActivate)
        drain()
        self.assertFalse(closed)
        window.active = False
        send(QEvent.Type.WindowDeactivate)
        drain()
        self.assertEqual(len(closed), 1)
        send(QEvent.Type.WindowDeactivate)
        drain()
        self.assertEqual(len(closed), 1)

        # Pending work from a previous showing cannot close a reopened window.
        send(QEvent.Type.WindowActivate)
        send(QEvent.Type.WindowDeactivate)
        window.hideEvent(QHideEvent())
        drain()
        self.assertFalse(window.activation_observed)
        self.assertEqual(len(closed), 1)
        send(QEvent.Type.WindowActivate)
        send(QEvent.Type.WindowDeactivate)
        drain()
        self.assertEqual(len(closed), 2)
        window.deleteLater()

    def test_14_focus_close_cancels_load_and_keeps_controller(self):
        from daemon import Controller
        controller = Controller(app)
        try:
            controller.request(json.dumps({"paths": [str(self.first)]}), False)
            window = controller.window
            app.processEvents()
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowActivate))
            # Inject only Qt events, without depending on offscreen activation.
            window.isActiveWindow = lambda: False
            generation = controller.generation
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowDeactivate))
            app.processEvents()
            self.assertFalse(controller.state()["visible"])
            self.assertEqual(controller.phase, "idle")
            self.assertGreater(controller.generation, generation)
            self.assertFalse(window.timer.isActive())
            self.assertFalse(window.frames)
            controller.pool.waitForDone(5000)
            app.processEvents()
            self.assertEqual(controller.phase, "idle")
            self.assertFalse(window.frames)  # Late decode cannot resurrect it.
            controller.request(json.dumps({"paths": [str(self.second)]}), False)
            self.assertTrue(controller.state()["visible"])
            self.assertFalse(window.activation_observed)
            self.assertEqual(controller.state()["pid"], os.getpid())
            # The private D-Bus daemon and its ordinary API remain available.
            self.assertEqual(call("GetState")["pid"], self.pid)
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            app.processEvents()
            controller.window.deleteLater()
            controller.deleteLater()

    def test_15_minimal_window_geometry_and_pixels(self):
        class Screen:
            def availableGeometry(self):
                return QRect(0, 0, 1000, 700)

        window = PreviewWindow(lambda: None, lambda _: None)
        try:
            from PyQt6.QtWidgets import QWidget, QLabel, QPushButton
            self.assertIn(window.canvas, window.findChildren(QWidget))
            self.assertFalse(window.findChildren(QLabel))
            self.assertEqual(len(window.findChildren(QPushButton)), 2)
            self.assertEqual(window.layout().contentsMargins().left(), 0)
            self.assertEqual(window.layout().contentsMargins().top(), 0)
            self.assertEqual(window.layout().contentsMargins().right(), 0)
            self.assertEqual(window.layout().contentsMargins().bottom(), 0)
            self.assertEqual(window.layout().spacing(), 0)
            self.assertTrue(window.windowFlags() & Qt.WindowType.FramelessWindowHint)
            self.assertFalse(window.testAttribute(Qt.WidgetAttribute.WA_TranslucentBackground))
            self.assertTrue(window.mask().isEmpty())
            # Before creating a native handle, exercise fitting against a
            # deterministic available area, including very small images.
            for width, height in ((640, 320), (320, 640), (4000, 2000),
                                  (2000, 4000), (101, 73), (1, 1)):
                image = QImage(width, height, QImage.Format.Format_RGB32)
                window.resize_to_image(image, Screen())
                self.assertLessEqual(window.width(), 968)
                self.assertLessEqual(window.height(), 668)
                scale = min(968/width, 668/height, 1.0)
                self.assertEqual(window.width(), max(1, int(width*scale)))
                self.assertEqual(window.height(), max(1, int(height*scale)))
            image = QImage(240, 120, QImage.Format.Format_RGB32)
            image.fill(QColor("#e32040"))
            window.resize_to_image(image, Screen())
            window.set_frames([(image, 100)], -1)
            window.show()
            app.processEvents()
            self.assertEqual(window.canvas.geometry(), window.rect())
            self.assertEqual(window.canvas.image_rect(), QRectF(window.rect()))
            captured = window.grab().toImage()
            for x, y in ((0, 0), (239, 0), (0, 119), (239, 119), (120, 60)):
                self.assertEqual(captured.pixelColor(x, y), QColor("#e32040"))
        finally:
            window.hide()
            window.deleteLater()

    def test_16_mouse_zones_visible_image_and_middle(self):
        actions = []
        window = PreviewWindow(lambda: actions.append("close"), lambda d: actions.append(d))
        try:
            window.resize(500, 300)
            image = QImage(400, 200, QImage.Format.Format_RGB32)
            window.set_frames([(image, 100)], -1)
            window.show()
            app.processEvents()
            canvas = window.canvas
            self.assertEqual(canvas.visible_image_rect(), QRectF(50, 50, 400, 200))
            # Targets are Canvas, as in the actual QWidget hierarchy.
            self.assertIs(window.childAt(QPoint(100, 150)), canvas)
            for point in (QPoint(90, 150), QPoint(250, 150), QPoint(410, 150)):
                QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=point)
            self.assertEqual(actions, [-1, "close", 1])
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(10, 150))
            QTest.mouseClick(canvas, Qt.MouseButton.RightButton, pos=QPoint(250, 150))
            self.assertEqual(actions, [-1, "close", 1])
            QTest.mouseClick(canvas, Qt.MouseButton.MiddleButton, pos=QPoint(250, 150))
            self.assertEqual(actions[-1], "close")
            # At zoom, zones use the visible crop, not offscreen image edges.
            canvas.zoom = 3
            canvas.pan = QPointF(100, 0)
            canvas.clamp_pan()
            self.assertEqual(canvas.visible_image_rect(), QRectF(canvas.rect()))
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(30, 150))
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(250, 150))
            QTest.mouseClick(canvas, Qt.MouseButton.LeftButton, pos=QPoint(470, 150))
            self.assertEqual(actions[-3:], [-1, "close", 1])
        finally:
            window.hide()
            window.deleteLater()

    def test_17_mouse_drag_threshold_and_release(self):
        canvas = Canvas()
        actions = []
        canvas.closeRequested.connect(lambda: actions.append("close"))
        canvas.navigateRequested.connect(actions.append)
        canvas.resize(400, 200)
        canvas.image = QImage(800, 400, QImage.Format.Format_RGB32)
        canvas.zoom = 3
        threshold = QApplication.startDragDistance()

        def move(point):
            event = QMouseEvent(QEvent.Type.MouseMove, QPointF(point), QPointF(point),
                                Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton,
                                Qt.KeyboardModifier.NoModifier)
            app.sendEvent(canvas, event)

        start = QPoint(200, 100)
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        move(start+QPoint(max(0, threshold-1), 0))
        self.assertEqual(canvas.pan, QPointF())
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=start)
        self.assertEqual(actions, ["close"])
        actions.clear()
        end = start+QPoint(threshold+30, 0)
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        move(end)
        move(start)  # Even returning to the origin must remain a drag.
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=start)
        self.assertFalse(actions)
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        move(end)
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=end)
        self.assertEqual(canvas.pan, QPointF(threshold+30, 0))
        self.assertFalse(actions)
        self.assertIsNone(canvas.press_position)
        # A far release without a move and a drag while fitted also do not click.
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=end)
        canvas.reset()
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        move(end)
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=end)
        self.assertFalse(actions)
        # Loading another image during a press cancels that gesture.
        QTest.mousePress(canvas, Qt.MouseButton.LeftButton, pos=start)
        canvas.reset()
        QTest.mouseRelease(canvas, Qt.MouseButton.LeftButton, pos=start)
        self.assertFalse(actions)
        canvas.deleteLater()

    def test_18_mouse_controller_navigation_gif_and_close(self):
        from daemon import Controller
        controller = Controller(app)
        try:
            controller.request(json.dumps({"paths": [str(self.gif), str(self.first)]}), False)

            def ready():
                for _ in range(100):
                    app.processEvents()
                    if controller.phase != "loading":
                        break
                    QTest.qWait(10)
                self.assertEqual(controller.phase, "ready")

            ready()
            canvas = controller.window.canvas
            self.assertEqual(len(controller.window.frames), 2)
            controller.window.advance_frame()

            def click(fraction, button=Qt.MouseButton.LeftButton):
                rect = canvas.visible_image_rect()
                point = QPoint(round(rect.left()+rect.width()*fraction), round(rect.center().y()))
                QTest.mouseClick(canvas, button, pos=point)

            click(0.9)
            ready()
            self.assertEqual(controller.path, str(self.first))
            click(0.1)
            ready()
            self.assertEqual(controller.path, str(self.gif))
            click(0.5)
            self.assertFalse(controller.window.isVisible())
            self.assertFalse(controller.window.timer.isActive())
            self.assertEqual(controller.phase, "idle")
            self.assertEqual(controller.state()["pid"], os.getpid())
            controller.request(json.dumps({"paths": [str(self.gif)]}), False)
            ready()
            click(0.5, Qt.MouseButton.MiddleButton)
            self.assertFalse(controller.window.isVisible())
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            app.processEvents()
            controller.window.deleteLater()
            controller.deleteLater()

    def test_19_zoom_grows_window_until_available_bounds(self):
        window = PreviewWindow(lambda: None, lambda _: None)
        try:
            window.show()
            app.processEvents()
            canvas = window.canvas
            available = window.screen().availableGeometry()
            max_w, max_h = available.width()-32, available.height()-32

            def wheel(delta):
                mouse = QPointF(canvas.rect().center())
                event = QWheelEvent(mouse, mouse, QPoint(), QPoint(0, delta),
                                    Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier,
                                    Qt.ScrollPhase.NoScrollPhase, False)
                app.sendEvent(canvas, event)
                app.processEvents()

            # Small portrait: both dimensions grow, then shrink on zoom-out.
            image = QImage(120, 240, QImage.Format.Format_RGB32)
            window.set_frames([(image, 100)], -1)
            window.resize_to_image(image, window.screen())
            wheel(120)
            self.assertAlmostEqual(canvas.zoom, 1.15)
            self.assertEqual(window.width(), min(138, max_w))
            self.assertEqual(window.height(), min(276, max_h))
            wheel(-120)
            self.assertAlmostEqual(canvas.zoom, 1)
            self.assertLessEqual(abs(window.width()-120), 1)
            self.assertLessEqual(abs(window.height()-240), 1)

            # Initially downscaled portrait: growing the viewport must not
            # recompute fit_scale and amplify the requested zoom.
            image = QImage(1200, 2400, QImage.Format.Format_RGB32)
            window.set_frames([(image, 100)], -1)
            window.resize_to_image(image, window.screen())
            base = canvas.fit_scale()
            wheel(120)
            self.assertEqual(canvas.fit_scale(), base)
            self.assertAlmostEqual(canvas.image_rect().height(), 2400*base*1.15)
            self.assertEqual(window.width(), min(int(1200*base*1.15), max_w))
            self.assertEqual(window.height(), min(int(2400*base*1.15), max_h))
            for _ in range(30):
                wheel(120)
            self.assertEqual(window.width(), max_w)
            self.assertEqual(window.height(), max_h)
            self.assertEqual(canvas.zoom, 32)
            self.assertGreater(canvas.image_rect().height(), canvas.height())
            # Once bounded, wheel zoom keeps the cursor anchor in the crop.
            mouse = QPointF(canvas.rect().center())
            old = canvas.image_rect()
            fraction = (mouse.y()-old.top())/old.height()
            wheel(-120)
            new = canvas.image_rect()
            self.assertAlmostEqual((mouse.y()-new.top())/new.height(), fraction)
            QTest.keyClick(window, Qt.Key.Key_F)
            self.assertEqual(canvas.zoom, 1)
            self.assertEqual(window.width(), max(1, int(1200*base)))
            self.assertEqual(window.height(), max(1, int(2400*base)))
            self.assertEqual(canvas.pan, QPointF())
        finally:
            window.hide()
            window.deleteLater()

    def test_20_reopen_sizing_constraints_and_late_resize(self):
        window = PreviewWindow(lambda: None, lambda _: None)
        image = QImage(1200, 2400, QImage.Format.Format_RGB32)
        try:
            for _ in range(12):
                window.present(app.primaryScreen())
                window.set_frames([(image, 100)], -1)
                window.resize_to_image(image, window.screen())
                expected = window.size()
                self.assertEqual(window.minimumSize(), expected)
                self.assertEqual(window.maximumSize(), expected)
                # Emulate a late request for the old loading viewport through
                # QWidget's resize path. Native min/max are also published.
                window.resize(120, 80)
                window.windowHandle().resize(120, 80)
                app.processEvents()
                QTest.qWait(10)
                self.assertEqual(window.size(), expected)
                self.assertEqual(window.canvas.geometry(), window.rect())
                self.assertEqual(window.windowHandle().minimumSize(), expected)
                self.assertEqual(window.windowHandle().maximumSize(), expected)
                self.assertLessEqual(window.canvas.image_rect().width(), window.canvas.width()+0.01)
                self.assertLessEqual(window.canvas.image_rect().height(), window.canvas.height()+0.01)
                window.hide()
                window.clear_content("")
        finally:
            window.hide()
            window.deleteLater()

    def test_21_heic_real_decode_and_orientation(self):
        source = ROOT/"examples/02 — HEIC [тест].heic"
        self.assertEqual(source.read_bytes()[4:12], b"ftypheic")
        for suffix in (".heic", ".HEIC", ".heif", ".HEIF"):
            path = self.home/("HEIC пробел %20 [скобки]"+suffix)
            shutil.copyfile(source, path)
            result = LoadJob(1, str(path), [str(path)]).decode()
            self.assertEqual(result["decoded_size"], [120, 80])
            self.assertEqual(len(result["frames"]), 1)
            frame = result["frames"][0][0]
            self.assertGreater(frame.pixelColor(20, 40).red(), 180)
            self.assertGreater(frame.pixelColor(100, 40).green(), 150)
        rotated = ROOT/"examples/03 — HEIC rotated.heic"
        result = LoadJob(1, str(rotated), [str(rotated)]).decode()
        self.assertEqual(result["decoded_size"], [80, 120])
        bad = self.home/"broken.heic"
        bad.write_bytes(b"not a HEIC file")
        with self.assertRaisesRegex(ValueError, "Не удалось прочитать изображение"):
            LoadJob(1, str(bad), [str(bad)]).decode()

    def test_22_heic_dbus_open_close_and_error(self):
        source = ROOT/"examples/02 — HEIC [тест].heic"
        path = self.home/"фото %20.HEIC"
        shutil.copyfile(source, path)
        self.request("Open", path)
        state = self.ready()
        self.assertEqual(state["phase"], "ready")
        self.assertEqual(state["path"], str(path))
        self.assertEqual(state["frame_count"], 1)
        self.assertEqual(state["pid"], self.pid)
        call("Close")
        self.assertFalse(call("GetState")["visible"])
        bad = self.home/"dbus-broken.heic"
        bad.write_bytes(b"invalid")
        self.request("Open", bad)
        state = self.ready()
        self.assertEqual(state["phase"], "error")
        self.assertEqual(state["frame_count"], 0)
        self.assertEqual(state["pid"], self.pid)

    def test_23_heic_dolphin_mime_and_neighbours(self):
        import threading
        from core import neighbours
        from manage import generated, PROBE, APP
        directory = self.home/"heic-playlist"
        directory.mkdir()
        source = ROOT/"examples/02 — HEIC [тест].heic"
        files = [directory/"01.png", directory/"02.HEIC", directory/"03.heif"]
        Image.new("RGB", (10, 10)).save(files[0])
        for path in files[1:]:
            shutil.copyfile(source, path)
        self.assertEqual(neighbours(str(files[1]), threading.Event()), list(map(str, files)))
        mime = QMimeDatabase().mimeTypeForFile(str(files[1])).name()
        self.assertIn(mime, {"image/heif", "image/heic"})
        entries = generated()
        for entry in (PROBE, APP):
            declared = next(line.removeprefix("MimeType=").split(";")
                            for line in entries[entry].splitlines() if line.startswith("MimeType="))
            self.assertIn(mime, declared)
            self.assertIn("image/heif", declared)
            self.assertIn("image/heic", declared)

    def test_24_heic_cache_invalidation_and_fresh_playlist(self):
        from loader import ImageCache
        cache = ImageCache()
        directory = self.home/"cache-files"
        directory.mkdir()
        path = directory/"01.heic"
        shutil.copyfile(ROOT/"examples/02 — HEIC [тест].heic", path)
        first = LoadJob(1, str(path), [], cache).decode()
        self.assertFalse(first["cache_hit"])
        second = LoadJob(2, str(path), [], cache).decode()
        self.assertTrue(second["cache_hit"])
        self.assertEqual(second["decoded_size"], [120, 80])
        added = directory/"02.heif"
        shutil.copyfile(path, added)
        fresh = LoadJob(3, str(path), [], cache).decode()
        self.assertTrue(fresh["cache_hit"])
        self.assertEqual(fresh["playlist"], [str(path), str(added)])
        shutil.copyfile(ROOT/"examples/03 — HEIC rotated.heic", path)
        changed = LoadJob(4, str(path), [], cache).decode()
        self.assertFalse(changed["cache_hit"])
        self.assertEqual(changed["decoded_size"], [80, 120])
        path.write_bytes(b"broken HEIC")
        with self.assertRaises(ValueError):
            LoadJob(5, str(path), [], cache).decode()
        self.assertIsNone(cache.get(str(path), cache.signature(path)))

    def test_25_heic_cache_memory_lru_and_cancel(self):
        from loader import ImageCache
        cache = ImageCache(max_bytes=128, max_entries=2)
        image = QImage(4, 4, QImage.Format.Format_RGB32)
        result = {"frames": [(image, 100)], "loops": -1, "decoded_size": [4, 4]}
        for key in ("a", "b"):
            cache.put(key, (1,), result)
        self.assertIsNotNone(cache.get("a", (1,)))
        cache.put("c", (1,), result)
        self.assertIsNone(cache.get("b", (1,)))
        self.assertLessEqual(cache.bytes, 128)
        self.assertEqual(len(cache.entries), 2)
        cache.clear()
        self.assertEqual(cache.bytes, 0)
        self.assertFalse(cache.entries)
        path = ROOT/"examples/02 — HEIC [тест].heic"
        job = LoadJob(1, str(path), [], cache)
        job.cancelled.set()
        self.assertTrue(job.decode()["cancelled"])
        self.assertEqual(cache.bytes, 0)

    def test_26_heic_prefetch_navigation_and_focus_close(self):
        from daemon import Controller
        from unittest.mock import patch
        from loader import LoadJob
        controller = Controller(app)
        first = str(ROOT/"examples/02 — HEIC [тест].heic")
        second = str(ROOT/"examples/03 — HEIC rotated.heic")
        original = LoadJob.decode

        def slow_background(job):
            if job.generation == -1:
                time.sleep(0.04)
            return original(job)

        def wait_for(predicate):
            for _ in range(200):
                app.processEvents()
                if predicate():
                    return
                QTest.qWait(5)
            self.fail("HEIC prefetch timeout")

        try:
            with patch.object(LoadJob, "decode", slow_background):
                controller.request(json.dumps({"paths": [first, second]}), False)
                wait_for(lambda: controller.phase == "ready")
                self.assertIsNotNone(controller.prefetch_job)
                self.assertEqual(controller.prefetch_job.path, second)
                controller.navigate(1)
                self.assertIsNotNone(controller.waiting_prefetch)
                self.assertFalse(controller.jobs)  # No duplicate foreground decode.
                wait_for(lambda: controller.phase == "ready")
                self.assertEqual(controller.path, second)
                self.assertEqual(controller.playlist, [first, second])
                controller.navigate(-1)
                wait_for(lambda: controller.phase == "ready")
                self.assertTrue(controller.last_cache_hit)
                window = controller.window
                QApplication.sendEvent(window, QEvent(QEvent.Type.WindowActivate))
                window.isActiveWindow = lambda: False
                QApplication.sendEvent(window, QEvent(QEvent.Type.WindowDeactivate))
                app.processEvents()
                self.assertFalse(window.isVisible())
                self.assertEqual(controller.phase, "idle")
                self.assertFalse(controller.prefetch_queue)
                self.assertIsNone(controller.waiting_prefetch)
                controller.pool.waitForDone(5000)
                app.processEvents()
                self.assertFalse(window.frames)
                self.assertIsNone(controller.prefetch_job)
                self.assertTrue(controller.cache_idle_timer.isActive())
                controller.cache_idle_timer.start(1)
                wait_for(lambda: controller.cache.bytes == 0)
                self.assertEqual(controller.cache.bytes, 0)
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            app.processEvents()
            controller.window.deleteLater()
            controller.deleteLater()

    def test_27_close_during_promoted_heic_prefetch(self):
        from daemon import Controller
        from unittest.mock import patch
        original = LoadJob.decode
        controller = Controller(app)
        paths = [str(ROOT/"examples/02 — HEIC [тест].heic"),
                 str(ROOT/"examples/03 — HEIC rotated.heic")]

        def delayed(job):
            if job.generation == -1:
                time.sleep(0.06)
            return original(job)

        try:
            with patch.object(LoadJob, "decode", delayed):
                controller.request(json.dumps({"paths": paths}), False)
                for _ in range(200):
                    app.processEvents()
                    if controller.phase == "ready":
                        break
                    QTest.qWait(5)
                self.assertEqual(controller.phase, "ready")
                controller.navigate(1)
                self.assertIsNotNone(controller.waiting_prefetch)
                controller.close_view()
                controller.pool.waitForDone(5000)
                app.processEvents()
                self.assertEqual(controller.phase, "idle")
                self.assertFalse(controller.window.isVisible())
                self.assertFalse(controller.window.frames)
                self.assertIsNone(controller.waiting_prefetch)
                self.assertIsNone(controller.prefetch_job)
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            app.processEvents()
            controller.window.deleteLater()
            controller.deleteLater()

    def test_28_heic_avoids_blocking_size_probe_and_bounds_decoded_frame(self):
        from unittest.mock import patch
        from PyQt6.QtGui import QImageReader

        class ReaderWithoutSizeProbe:
            def __init__(self, path):
                self.reader = QImageReader(path)

            def size(self):
                raise AssertionError("HEIF size() preflight blocks the Python GUI")

            def __getattr__(self, name):
                return getattr(self.reader, name)

        with patch("loader.QImageReader", ReaderWithoutSizeProbe), patch("loader.MAX_EDGE", 64):
            for name in ("02 — HEIC [тест].heic", "03 — HEIC rotated.heic"):
                path = str(ROOT/"examples"/name)
                result = LoadJob(1, path, [path]).decode()
                self.assertEqual(max(result["decoded_size"]), 64)
                self.assertEqual(len(result["frames"]), 1)
                self.assertLessEqual(result["frames"][0][0].sizeInBytes(), 64*64*4)

    def test_29_navigation_retains_frame_without_loading_flash(self):
        from daemon import Controller
        from unittest.mock import patch
        original = LoadJob.decode
        controller = Controller(app)
        paths = [str(ROOT/"examples/02 — HEIC [тест].heic"),
                 str(ROOT/"examples/03 — HEIC rotated.heic")]

        def delayed(job):
            if job.path == paths[1]:
                time.sleep(0.05)
            return original(job)

        def ready():
            for _ in range(200):
                app.processEvents()
                if controller.phase == "ready":
                    return
                QTest.qWait(5)
            self.fail("Retained-frame navigation timed out")

        try:
            with patch.object(LoadJob, "decode", delayed):
                controller.request(json.dumps({"paths": paths}), False)
                ready()
                canvas = controller.window.canvas
                previous = canvas.image.cacheKey()
                previous_size = controller.window.size()
                controller.navigate(1)
                self.assertEqual(controller.phase, "loading")
                self.assertTrue(controller.window.frames)
                self.assertEqual(canvas.message, "")
                self.assertEqual(canvas.image.cacheKey(), previous)
                self.assertEqual(controller.window.size(), previous_size)
                ready()
                self.assertNotEqual(canvas.image.cacheKey(), previous)
                self.assertEqual((canvas.image.width(), canvas.image.height()), (80, 120))
                self.assertEqual(canvas.message, "")
                controller.navigate(-1)
                self.assertEqual(canvas.message, "")
                controller.close_view()
                controller.pool.waitForDone(5000)
                app.processEvents()
                self.assertFalse(controller.window.frames)
                self.assertFalse(controller.window.isVisible())
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            app.processEvents()
            controller.window.deleteLater()
            controller.deleteLater()

    def audio_wait(self, predicate, timeout=3000):
        for _ in range(timeout//10):
            app.processEvents()
            if predicate():
                return
            QTest.qWait(10)
        self.fail("Audio event timeout")

    def audio_window(self, navigate=None):
        from audio import AudioWindow
        holder = []
        def close():
            holder[0].stop_audio()
            holder[0].hide()
        window = AudioWindow(close, navigate)
        holder.append(window)
        window.output.setVolume(0)
        window.focusLost.connect(close)
        window.present(app.primaryScreen())
        return window

    def audio_cleanup(self, window):
        window.stop_audio()
        window.hide()
        window.metadata_pool.waitForDone(5000)
        app.processEvents()
        window.deleteLater()

    def test_30_audio_routing_and_lazy_initialization(self):
        from core import media_kind, neighbours
        import threading
        self.assertFalse(call("GetState")["audio_initialized"])
        for suffix in ("mp3", "FLAC", "ogg", "WAV", "m4a", "AAC"):
            self.assertEqual(media_kind("file."+suffix), "audio")
        self.assertEqual(media_kind("image.HEIC"), "image")
        images = neighbours(str(self.first), threading.Event())
        self.assertFalse(any(Path(p).suffix.lower() == ".mp3" for p in images))
        code = "import daemon,sys; assert 'audio' not in sys.modules; assert 'PyQt6.QtMultimedia' not in sys.modules"
        result = subprocess.run([sys.executable, "-B", "-c", code], cwd=ROOT,
                                env=self.env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_31_actual_audio_codecs_autoplay_and_stop(self):
        from PyQt6.QtMultimedia import QAudioBufferOutput
        window = self.audio_window()
        probe = QAudioBufferOutput(window)
        window.player.setAudioBufferOutput(probe)
        buffers = []
        probe.audioBufferReceived.connect(lambda buffer: buffers.append(buffer.byteCount()))
        try:
            for extension in ("mp3", "flac", "ogg", "wav", "m4a", "aac"):
                with self.subTest(extension=extension):
                    buffers.clear()
                    window.open_audio(str(ROOT/"examples/audio"/("preview."+extension)))
                    self.audio_wait(lambda: window.phase == "error" or
                                    (window.player.position() > 50 and sum(buffers) > 0))
                    self.assertEqual(window.phase, "ready", window.error)
                    self.assertTrue(window.snapshot()["playing"])
                    self.assertGreater(window.player.duration(), 5000)
                    self.assertGreater(sum(buffers), 0)
                    window.stop_audio()
                    self.assertTrue(window.snapshot()["source_empty"])
                    self.assertFalse(window.snapshot()["playing"])
        finally:
            self.audio_cleanup(window)

    def test_32_audio_controls_seek_volume_and_no_false_close(self):
        from audio import AudioWindow
        window = self.audio_window()
        try:
            window.open_audio(str(ROOT/"examples/audio/preview.wav"))
            self.audio_wait(lambda: window.player.position() > 50 and window.seek_bar.isEnabled())
            QTest.mouseClick(window.play_button, Qt.MouseButton.LeftButton)
            self.assertEqual(window.player.playbackState().name, "PausedState")
            QTest.mouseClick(window.play_button, Qt.MouseButton.LeftButton)
            self.assertEqual(window.player.playbackState().name, "PlayingState")
            window.player.pause()
            seek = window.seek_bar
            QTest.mouseClick(seek, Qt.MouseButton.LeftButton, pos=QPoint(seek.width()*3//4, 14))
            self.assertGreater(window.player.position(), 7000)
            QTest.mousePress(seek, Qt.MouseButton.LeftButton, pos=QPoint(seek.width()//2, 14))
            target = seek.position_ms
            window.position_changed(1)
            self.assertEqual(seek.position_ms, target)
            end = QPoint(seek.width()//4, 14)
            event = QMouseEvent(QEvent.Type.MouseMove, QPointF(end), QPointF(end),
                                Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)
            app.sendEvent(seek, event)
            QTest.mouseRelease(seek, Qt.MouseButton.LeftButton, pos=end)
            self.assertFalse(seek.dragging)
            self.assertLess(window.player.position(), 4000)
            self.assertGreaterEqual(seek.height(), 20)
            QTest.mouseClick(window.speaker_button, Qt.MouseButton.LeftButton)
            self.assertTrue(window.volume_slider.isVisible())
            self.assertFalse(window.volume_slider.isWindow())
            window.set_volume(0.5)
            for widget in (window.canvas, window.controls, window.play_button, window.volume_slider):
                position = QPointF(10, 10)
                event = QWheelEvent(position, position, QPoint(), QPoint(0, 120), Qt.MouseButton.NoButton,
                                    Qt.KeyboardModifier.NoModifier, Qt.ScrollPhase.NoScrollPhase, False)
                previous = window.output.volume()
                app.sendEvent(widget, event)
                self.assertAlmostEqual(window.output.volume(), previous, places=5)
                self.assertTrue(window.isVisible())
            previous = window.output.volume()
            smooth = QWheelEvent(QPointF(10, 10), QPointF(10, 10), QPoint(0, 1), QPoint(),
                                 Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier,
                                 Qt.ScrollPhase.ScrollUpdate, False)
            app.sendEvent(window.speaker_button, smooth)
            self.assertGreater(window.output.volume(), previous)
            self.assertLess(window.output.volume()-previous, 0.01)
            window.player.setPosition(3000)
            position = QPointF(10,10)
            wheel = QWheelEvent(position,position,QPoint(),QPoint(0,120),Qt.MouseButton.NoButton,
                                Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.ScrollUpdate,False)
            previous = window.output.volume()
            app.sendEvent(seek,wheel)
            self.assertEqual(window.player.position(),4000)
            self.assertEqual(window.output.volume(),previous)
            wheel = QWheelEvent(position,position,QPoint(),QPoint(0,-120),Qt.MouseButton.NoButton,
                                Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.ScrollUpdate,False)
            app.sendEvent(seek,wheel)
            self.assertEqual(window.player.position(),3000)
            window.set_volume(4)
            self.assertEqual(window.output.volume(), 1)
            window.set_volume(-1)
            self.assertEqual(window.output.volume(), 0)
            window.set_volume(0.35)
            window.save_volume()
            other = AudioWindow(lambda: None)
            self.assertAlmostEqual(other.output.volume(), 0.35, places=5)
            self.audio_cleanup(other)
            self.assertTrue(window.isVisible())
        finally:
            self.audio_cleanup(window)

    def test_33_audio_metadata_covers_limits_and_unknown_time(self):
        from audio_metadata import MetadataJob, decode_cover, MAX_COVER_BYTES
        from audio import time_text
        for extension in ("mp3", "flac", "ogg", "wav", "m4a", "aac"):
            job = MetadataJob(1, str(ROOT/"examples/audio"/("preview."+extension)))
            results = []
            job.signals.done.connect(lambda _, result: results.append(result))
            job.run()
            self.assertFalse(results[0]["error"])
            self.assertEqual(not results[0]["cover"].isNull(), extension in {"mp3", "flac", "ogg", "m4a"})
        self.assertTrue(decode_cover(b"x"*(MAX_COVER_BYTES+1)).isNull())
        self.assertTrue(decode_cover(b"bad image").isNull())
        self.assertEqual(time_text(None), "--:--")
        self.assertEqual(time_text(3661000), "01:01:01")

    def test_34_audio_keyboard_focus_close_reopen_and_errors(self):
        window = self.audio_window()
        path = str(ROOT/"examples/audio/preview.wav")
        try:
            window.open_audio(path)
            self.audio_wait(lambda: window.player.position() > 50)
            window.player.setPosition(6000)
            window.open_audio(path)
            self.audio_wait(lambda: window.snapshot()["playing"])
            self.assertLess(window.player.position(), 500)
            QTest.keyClick(window.play_button, Qt.Key.Key_Space)
            self.assertFalse(window.isVisible())
            self.assertTrue(window.snapshot()["source_empty"])
            self.assertFalse(window.snapshot()["playing"])
            window.present(app.primaryScreen())
            window.open_audio(path)
            self.audio_wait(lambda: window.snapshot()["playing"])
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowActivate))
            window.isActiveWindow = lambda: False
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowDeactivate))
            app.processEvents()
            self.assertFalse(window.isVisible())
            self.assertTrue(window.snapshot()["source_empty"])
            window.present(app.primaryScreen())
            window.open_audio(str(ROOT/"examples/audio/broken.mp3"))
            self.audio_wait(lambda: window.phase == "error")
            self.assertTrue(window.error)
            self.assertFalse(window.snapshot()["playing"])
            QTest.keyClick(window.seek_bar, Qt.Key.Key_Escape)
            self.assertFalse(window.isVisible())
        finally:
            self.audio_cleanup(window)

    def test_35_audio_dbus_to_image_and_resident_lifecycle(self):
        path = ROOT/"examples/audio/preview.wav"
        self.request("Open", path)
        for _ in range(300):
            state = call("GetState")
            if state.get("audio", {}).get("playing"):
                break
            time.sleep(0.01)
        self.assertEqual(state["module"], "audio")
        self.assertTrue(state["audio"]["playing"], state)
        self.assertEqual(state["pid"], self.pid)
        call("Close")
        self.assertTrue(call("GetState")["audio"]["source_empty"])
        self.request("Open", path)
        self.assertEqual(call("GetState")["pid"], self.pid)
        self.request("Open", self.first)
        state = self.ready()
        self.assertEqual(state["module"], "image")
        self.assertEqual(state["phase"], "ready")
        self.assertFalse(state["audio_playing"])
        self.assertEqual(state["frame_count"], 1)

    def test_36_portable_repeated_upgrade(self):
        isolated = self.home/"portable-upgrade"
        env = {**self.env, "HOME": str(isolated), "XDG_DATA_HOME": str(isolated/"data"),
               "XDG_CONFIG_HOME": str(isolated/"config"), "XDG_STATE_HOME": str(isolated/"state")}
        for command in ("install", "update", "update", "uninstall"):
            result = subprocess.run([sys.executable, "-B", str(ROOT/"manage.py"), command, "--isolated"],
                                    env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((isolated/"data/kdeglance2").exists())

    def test_37_volume_persists_across_process_restart(self):
        window = self.audio_window()
        try:
            window.set_volume(0.42)
            window.save_volume()
            code = ("from PyQt6.QtWidgets import QApplication; from audio import AudioWindow; "
                    "app=QApplication(['volume-probe']); window=AudioWindow(lambda:None); "
                    "print(window.output.volume()); window.stop_audio()")
            result = subprocess.run([sys.executable, "-B", "-c", code], cwd=ROOT,
                                    env=os.environ.copy(), capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertAlmostEqual(float(result.stdout.strip().splitlines()[-1]), 0.42, places=5)
        finally:
            self.audio_cleanup(window)


    def test_38_compact_layout_and_transient_vertical_volume(self):
        window = self.audio_window()
        try:
            area = app.primaryScreen().availableGeometry()
            edge = min(760, area.width()-32, area.height()-32)
            self.assertEqual((window.width(), window.height()), (edge, edge))
            self.assertFalse(window.windowFlags() & Qt.WindowType.WindowStaysOnTopHint)
            self.assertTrue(window.windowFlags() & Qt.WindowType.FramelessWindowHint)
            self.assertEqual(window.volume_slider.orientation(), Qt.Orientation.Vertical)
            for size in (350,380,400,760):
                window.apply_size(size, size)
                for text in ("00:00", "59:59", "1:23:45", "01:23:45"):
                    window.elapsed.setText(text)
                    window.total.setText(text)
                    app.processEvents()
                    children = [window.play_button, window.elapsed, window.seek_bar, window.total, window.speaker_button]
                    rects = []
                    for child in children:
                        rect = QRect(child.mapTo(window.controls, QPoint()), child.size())
                        self.assertTrue(window.controls.rect().contains(rect), (size, text, rect))
                        rects.append(rect)
                    for left, right in zip(rects, rects[1:]):
                        self.assertLess(left.right(), right.left(), (size, text, left, right))
                    for label in (window.elapsed, window.total):
                        self.assertGreaterEqual(label.width(), label.sizeHint().width())
                    self.assertGreaterEqual(window.seek_bar.width(), 44)
                    self.assertGreaterEqual(window.seek_bar.height(), 20)
            window.set_volume(.65)
            app.processEvents()
            self.assertTrue(window.volume_panel.isVisible())
            self.assertEqual(window.percent.text(), "65%")
            self.assertTrue(window.canvas.rect().contains(window.volume_panel.geometry()))
            slider_origin = window.volume_slider.mapTo(window.canvas,QPoint())
            speaker_center = window.speaker_button.mapTo(window.canvas,window.speaker_button.rect().center())
            self.assertLess(slider_origin.y()+window.volume_slider.height(),window.controls.geometry().top())
            self.assertLessEqual(abs((slider_origin.x()+window.volume_slider.width()/2)-speaker_center.x()),1)
            self.assertFalse(hasattr(window,"volume_icon"))
            self.assertTrue(window.volume_panel.geometry().contains(speaker_center))
            self.assertFalse(window.volume_panel.isWindow())
            window.volume_slider.setSliderDown(True)
            window.hide_volume_panel()
            self.assertTrue(window.volume_panel.isVisible())
            self.assertEqual(window.volume_opacity.opacity(), 1)
            window.volume_slider.setSliderDown(False)
            window.hide_volume_panel()
            QTest.qWait(280)
            self.assertFalse(window.volume_panel.isVisible())
            self.assertTrue(window.isVisible())
            QTest.mouseClick(window.speaker_button, Qt.MouseButton.LeftButton)
            self.assertTrue(window.volume_panel.isVisible())
            QTest.keyClick(window.volume_slider, Qt.Key.Key_Escape)
            self.assertFalse(window.isVisible())
            self.assertFalse(window.percent_timer.isActive())
        finally:
            self.audio_cleanup(window)

    def test_39_audio_artwork_zones_and_unframed_cover(self):
        navigation = []
        window = self.audio_window(navigation.append)
        try:
            window.canvas.image = QImage(200, 200, QImage.Format.Format_RGB32)
            window.canvas.image.fill(QColor("#e32040"))
            window.canvas.update()
            app.processEvents()
            pixels = window.grab().toImage()
            for x, y in ((0, 0), (window.width()-1, 0), (0, window.height()-120)):
                self.assertEqual(pixels.pixelColor(x, y), QColor("#e32040"))
            window.open_audio(str(ROOT/"examples/audio/preview.wav"))
            self.audio_wait(lambda: window.snapshot()["playing"])
            QTest.mouseClick(window.canvas, Qt.MouseButton.LeftButton, pos=QPoint(5, 100))
            QTest.mouseClick(window.canvas, Qt.MouseButton.LeftButton, pos=QPoint(window.width()-5, 100))
            self.assertEqual(navigation, [-1, 1])
            self.assertTrue(window.isVisible())
            # Leaving a cover gesture and returning to its start is still a drag.
            start = QPoint(20, 100)
            QTest.mousePress(window.canvas, Qt.MouseButton.LeftButton, pos=start)
            move = QMouseEvent(QEvent.Type.MouseMove, QPointF(100, 100), QPointF(100, 100),
                               Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)
            app.sendEvent(window.canvas, move)
            QTest.mouseRelease(window.canvas, Qt.MouseButton.LeftButton, pos=start)
            self.assertEqual(navigation, [-1, 1])
            QTest.mouseClick(window.canvas, Qt.MouseButton.LeftButton, pos=QPoint(window.width()//2, 100))
            self.assertFalse(window.isVisible())
            self.assertTrue(window.player.source().isEmpty())
        finally:
            self.audio_cleanup(window)

    def test_43_images_sync_displayed_file_and_commit_on_space(self):
        from daemon import Controller
        from PyQt6.QtCore import QObject, pyqtSignal
        selected, finished = [], []

        class Selection(QObject):
            idle = pyqtSignal()
            def __init__(self, path):
                super().__init__()
                self.path = self.selected = path
            def select(self, path):
                self.path = self.selected = path
                selected.append(path)
            def finish(self, path):
                finished.append(path)
                self.idle.emit()

        controller = Controller(app)
        def ready():
            self.audio_wait(lambda: controller.phase == "ready")
        try:
            controller.request(json.dumps({"paths": [str(self.first), str(self.second), str(self.gif)]}), False)
            ready()
            controller.image_selection = Selection(str(self.first))
            controller.window.present = lambda *_: self.fail("Navigation must not reactivate the viewer")
            QTest.keyClick(controller.window, Qt.Key.Key_Right)
            ready()
            self.assertEqual(selected, [str(self.second)])
            QTest.keyClick(controller.window, Qt.Key.Key_Right)
            ready()
            QTest.keyClick(controller.window, Qt.Key.Key_Left)
            ready()
            self.assertEqual(selected, [str(self.second), str(self.gif), str(self.second)])
            self.assertTrue(controller.state()["dolphin_selection_synced"])
            # A cancelled decode must not replace the last image actually displayed.
            controller.load(str(self.first))
            QTest.keyClick(controller.window, Qt.Key.Key_Space)
            self.assertFalse(controller.window.isVisible())
            self.assertEqual(finished, [str(self.second)])
            self.assertIsNone(controller.image_selection)
            controller.pool.waitForDone(5000)
            app.processEvents()
            self.assertEqual(selected[-1], str(self.second))
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            controller.window.deleteLater()
            controller.deleteLater()

    def video_window(self, navigate=None):
        from video import VideoWindow
        holder = []
        def close():
            holder[0].stop_video()
            holder[0].hide()
        window = VideoWindow(close, navigate)
        holder.append(window)
        window.output.setVolume(0)
        window.focusLost.connect(close)
        window.present(app.primaryScreen())
        return window

    def video_cleanup(self, window):
        window.stop_video()
        window.hide()
        app.processEvents()
        window.deleteLater()

    def test_44_video_real_frames_audio_and_containers(self):
        from PyQt6.QtMultimedia import QAudioBufferOutput
        from core import media_kind, VIDEO_EXTENSIONS
        for extension in VIDEO_EXTENSIONS:
            self.assertEqual(media_kind("movie"+extension.upper()), "video")
        code = "import daemon,sys; assert 'video' not in sys.modules; assert 'PyQt6.QtMultimedia' not in sys.modules"
        subprocess.run([sys.executable, "-B", "-c", code], cwd=ROOT, env=self.env, check=True)
        window = self.video_window()
        buffers = []
        probe = QAudioBufferOutput(window)
        probe.audioBufferReceived.connect(lambda buffer: buffers.append(buffer.byteCount()))
        window.player.setAudioBufferOutput(probe)
        try:
            for name in ("preview.mp4", "preview.mkv", "preview.webm", "preview.avi", "preview.mov", "portrait-silent.mp4"):
                with self.subTest(name=name):
                    buffers.clear()
                    window.open_video(str(ROOT/"examples/video"/name))
                    self.audio_wait(lambda: window.phase == "error" or
                        (window.frames_received > 2 and window.player.position() > 100), timeout=5000)
                    self.assertFalse(window.error, window.error)
                    self.assertTrue(window.snapshot()["has_frame"])
                    self.assertTrue(window.player.hasVideo())
                    self.assertGreater(window.player.duration(), 5000)
                    self.assertEqual(window.snapshot()["frame_size"], [240,400] if name.startswith("portrait") else [640,360])
                    if not name.startswith("portrait"):
                        self.audio_wait(lambda: any(buffers))
                    else:
                        self.assertFalse(window.player.hasAudio())
                    picture = window.canvas.grab().toImage()
                    self.assertNotEqual(picture.pixelColor(picture.width()//2, picture.height()//2), QColor("#0b1017"))
                    window.stop_video()
                    self.assertTrue(window.player.source().isEmpty())
                    self.assertFalse(window.canvas.frame.isValid())
        finally:
            self.video_cleanup(window)

    def test_45_video_controls_close_and_errors(self):
        navigation = []
        window = self.video_window(navigation.append)
        try:
            path = str(ROOT/"examples/video/preview.mp4")
            window.open_video(path)
            self.audio_wait(lambda: window.frames_received > 2 and window.seek_bar.isEnabled())
            QTest.mouseClick(window.play_button, Qt.MouseButton.LeftButton)
            self.assertEqual(window.player.playbackState().name, "PausedState")
            position = window.player.position()
            QTest.qWait(100)
            self.assertLessEqual(abs(window.player.position()-position), 30)
            QTest.mouseClick(window.seek_bar, Qt.MouseButton.LeftButton, pos=QPoint(window.seek_bar.width()*2//3, 14))
            self.audio_wait(lambda: window.player.position() > 6000)
            QTest.mouseClick(window.play_button, Qt.MouseButton.LeftButton)
            self.assertEqual(window.player.playbackState().name, "PlayingState")
            window.set_volume(.65)
            self.assertTrue(window.volume_panel.isVisible())
            self.assertEqual(window.percent.text(), "65%")
            self.assertFalse(hasattr(window, "volume_icon"))
            QTest.qWait(1850)
            self.assertFalse(window.volume_panel.isVisible())
            self.assertTrue(window.isVisible())
            self.assertFalse(navigation)
            QTest.mouseClick(window.canvas, Qt.MouseButton.LeftButton, pos=QPoint(5, 100))
            QTest.mouseClick(window.canvas, Qt.MouseButton.LeftButton, pos=QPoint(window.width()-5, 100))
            self.assertEqual(navigation, [-1, 1])
            QTest.keyClick(window, Qt.Key.Key_Space)
            self.assertFalse(window.isVisible())
            self.assertTrue(window.player.source().isEmpty())
            self.assertFalse(window.canvas.frame.isValid())
            window.present(app.primaryScreen())
            window.open_video(str(ROOT/"examples/video/broken.mp4"))
            self.audio_wait(lambda: window.phase == "error")
            self.assertTrue(window.error)
            self.assertFalse(window.seek_bar.isEnabled())
            audio_only = self.home/"audio-only.mp4"
            shutil.copy2(ROOT/"examples/audio/preview.wav", audio_only)
            window.open_video(str(audio_only))
            self.audio_wait(lambda: window.phase == "error")
            self.assertIn("видеодорожки", window.error)
            window.open_video(path)
            self.audio_wait(lambda: window.frames_received > 2)
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowActivate))
            window.isActiveWindow = lambda: False
            QApplication.sendEvent(window, QEvent(QEvent.Type.WindowDeactivate))
            app.processEvents()
            self.assertFalse(window.isVisible())
            self.assertTrue(window.player.source().isEmpty())
        finally:
            self.video_cleanup(window)

    def test_46_video_dbus_routes_stop_other_modules_and_toggle(self):
        video = str(ROOT/"examples/video/preview.mp4")
        audio = str(ROOT/"examples/audio/preview.wav")
        pid = call("GetState")["pid"]
        for path, module in ((video,"video"), (audio,"audio"), (video,"video"), (str(self.first),"image")):
            call("Open", {"paths": [path]})
            state = {}
            def ready():
                nonlocal state
                state = call("GetState")
                return state["module"] == module and state["phase"] == "ready" and (
                    module != "video" or state["video"]["frames_received"] > 2)
            self.audio_wait(ready, timeout=5000)
            self.assertEqual(state["pid"], pid)
            self.assertTrue(state["visible"])
        call("Open", {"paths": [video]})
        self.audio_wait(lambda: call("GetState").get("video", {}).get("frames_received", 0) > 2)
        QTest.qWait(300)
        state = call("Toggle", {"paths": [video]})
        self.assertFalse(state["visible"])
        self.assertTrue(state["video"]["source_empty"])
        self.assertFalse(state["video"]["has_frame"])
        from manage import generated, PROBE, RUNTIME
        self.assertIn("video/mp4;", generated()[PROBE])
        self.assertIn("video.py", RUNTIME)

    def test_47_video_transitions_release_previous_player_and_keep_one_window(self):
        from daemon import Controller
        controller = Controller(app)
        video = str(ROOT/"examples/video/preview.mp4")
        audio = str(ROOT/"examples/audio/preview.wav")
        try:
            self.assertIsNone(controller.video)
            for path, mode in ((video,"video"), (audio,"audio"), (video,"video"), (str(self.first),"image")):
                controller.request(json.dumps({"paths": [path]}), False)
                if mode == "video":
                    controller.video.output.setVolume(0)
                    self.audio_wait(lambda: controller.video.frames_received > 2)
                elif mode == "audio":
                    controller.audio.output.setVolume(0)
                    self.audio_wait(lambda: controller.audio.snapshot()["playing"])
                else:
                    self.audio_wait(lambda: controller.phase == "ready")
                self.assertEqual(controller.mode, mode)
                windows = [controller.window, controller.audio, controller.video]
                self.assertEqual(sum(bool(w and w.isVisible()) for w in windows), 1)
                if mode != "video":
                    self.assertTrue(controller.video.player.source().isEmpty())
                    self.assertFalse(controller.video.canvas.frame.isValid())
                if controller.audio and mode != "audio":
                    self.assertTrue(controller.audio.player.source().isEmpty())
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            if controller.audio:
                self.audio_cleanup(controller.audio)
            if controller.video:
                self.video_cleanup(controller.video)
            controller.window.deleteLater()
            controller.deleteLater()

    def test_48_video_window_fits_source_size_rotation_and_screen(self):
        window = self.video_window()
        try:
            for name, dimensions in (("preview.mp4", (640,360)), ("portrait-silent.mp4", (240,400)),
                                      ("rotated.mp4", (360,640)), ("large-silent.mp4", (1920,1080))):
                with self.subTest(name=name):
                    window.open_video(str(ROOT/"examples/video"/name))
                    self.audio_wait(lambda: window.frames_received > 2, timeout=5000)
                    area = window.screen().availableGeometry()
                    scale = min(1, (area.width()-32)/dimensions[0], (area.height()-32)/dimensions[1])
                    expected = (int(dimensions[0]*scale), int(dimensions[1]*scale))
                    self.assertEqual(window.snapshot()["display_size"], list(dimensions))
                    self.assertEqual((window.width(), window.height()), expected)
                    self.assertEqual(window.size(), window.windowHandle().size())
                    controls = [c for c in (window.play_button, window.elapsed, window.seek_bar, window.total,
                                           window.rotate_button, window.fullscreen_button, window.speaker_slot) if c.isVisible()]
                    for left, right in zip(controls, controls[1:]):
                        self.assertLess(left.geometry().right(), right.geometry().left())
                    self.assertGreaterEqual(controls[0].x(), 0)
                    self.assertLess(controls[-1].geometry().right(), window.width())
                    window.set_volume(.65)
                    slider = window.volume_slider.mapTo(window.canvas, window.volume_slider.rect().center())
                    speaker = window.speaker_button.mapTo(window.canvas, window.speaker_button.rect().center())
                    self.assertLessEqual(abs(slider.x()-speaker.x()), 1)
        finally:
            self.video_cleanup(window)

    def test_49_photo_transform_controls_are_reversible(self):
        window = PreviewWindow(lambda: None, lambda _: None)
        try:
            image = QImage(120, 80, QImage.Format.Format_RGB32)
            image.fill(QColor("red"))
            image.setPixelColor(0,0,QColor("blue"))
            window.set_frames([(image,100)], -1)
            window.resize_to_image(image, app.primaryScreen())
            window.show()
            window.flip_button.click()
            self.assertEqual(window.canvas.image, image.mirrored(True,False))
            window.rotate_button.click()
            self.assertEqual((window.canvas.image.width(),window.canvas.image.height()), (80,120))
            self.assertEqual(window.image_rotation,270)
            for _ in range(3): window.rotate_button.click()
            self.assertEqual(window.canvas.image, image.mirrored(True,False))
            window.flip_button.click()
            self.assertEqual(window.canvas.image,image)
            self.assertEqual(image.pixelColor(0,0),QColor("blue"))
        finally:
            window.hide()
            window.deleteLater()

    def test_50_video_wheel_zones_rotate_fullscreen_and_zoom(self):
        window = self.video_window()
        def wheel(widget, delta):
            point = QPointF(widget.rect().center())
            event = QWheelEvent(point,QPointF(widget.mapToGlobal(point.toPoint())),QPoint(),QPoint(0,delta),
                                Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.ScrollUpdate,False)
            app.sendEvent(widget,event)
        try:
            window.open_video(str(ROOT/"examples/video/preview.mp4"))
            self.audio_wait(lambda: window.frames_received > 2 and window.player.isSeekable())
            window.player.pause()
            window.player.setPosition(3000)
            self.audio_wait(lambda: window.player.position() == 3000)
            volume = window.output.volume()
            wheel(window.seek_bar,120)
            self.assertEqual(window.player.position(),4000)
            wheel(window.seek_bar,-120)
            self.assertEqual(window.player.position(),3000)
            self.assertEqual(window.output.volume(),volume)
            wheel(window.speaker_button,120)
            self.assertAlmostEqual(window.output.volume(),volume+.05,places=5)
            volume = window.output.volume()
            wheel(window.canvas,120)
            self.assertGreater(window.canvas.zoom,1)
            self.assertEqual(window.player.position(),3000)
            self.assertEqual(window.output.volume(),volume)
            wheel(window.total,120)
            self.assertEqual(window.output.volume(),volume)
            window.rotate_button.click()
            self.assertEqual(window.manual_rotation,270)
            self.assertEqual(window.snapshot()["display_size"],[360,640])
            self.assertEqual(window.canvas.zoom,1)
            for _ in range(3): window.rotate_button.click()
            self.assertEqual(window.snapshot()["display_size"],[640,360])
            window.fullscreen_button.click()
            self.assertTrue(window.isFullScreen())
            self.assertIsNone(window.target_size)
            QTest.keyClick(window,Qt.Key.Key_Escape)
            self.assertFalse(window.isFullScreen())
            self.assertTrue(window.isVisible())
            self.assertEqual((window.width(),window.height()),(640,360))
        finally:
            self.video_cleanup(window)

    def test_51_all_modules_hover_fade_and_window_drag(self):
        image = PreviewWindow(lambda: None, lambda _: None)
        picture = QImage(400,300,QImage.Format.Format_RGB32)
        picture.fill(QColor("red"))
        image.set_frames([(picture,100)],-1)
        image.resize_to_image(picture,app.primaryScreen())
        image.show()
        windows = [image]
        try:
            for factory in (lambda: image, self.audio_window, self.video_window):
                for opened in windows:
                    opened.hide()
                window = factory()
                if window is image: image.show()
                else: windows.append(window)
                app.processEvents()
                QTest.qWait(40)
                with self.subTest(window=type(window).__name__):
                    ui = window.interactions
                    ui.activity(window.mapToGlobal(QPoint(20,20)))
                    self.assertEqual(ui.opacity,1)
                    self.assertEqual(ui.idle.interval(),3000)
                    self.assertEqual(ui.fade.duration(),1000)
                    ui.idle.start(30)
                    QTest.qWait(350)
                    self.assertGreater(ui.opacity,0)
                    self.assertLess(ui.opacity,1)
                    QTest.qWait(850)
                    self.assertAlmostEqual(ui.opacity,0,places=3)
                    app.sendEvent(window.canvas,QEvent(QEvent.Type.Enter))
                    self.assertEqual(ui.opacity,0)
                    ui.activity(window.mapToGlobal(QPoint(30,30)))
                    self.assertEqual(ui.opacity,1)
                    app.sendEvent(window,QEvent(QEvent.Type.Leave))
                    QTest.qWait(1050)
                    self.assertEqual(ui.opacity,0)
                    ui.activity(window.mapToGlobal(QPoint(20,20)))
                    requests=[]
                    ui.start_move = lambda:requests.append(True) or True
                    QTest.mousePress(window.canvas,Qt.MouseButton.LeftButton,pos=QPoint(30,30))
                    move = QMouseEvent(QEvent.Type.MouseMove,QPointF(80,70),
                        QPointF(window.canvas.mapToGlobal(QPoint(80,70))),Qt.MouseButton.NoButton,
                        Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier)
                    app.sendEvent(window.canvas,move)
                    QTest.mouseRelease(window.canvas,Qt.MouseButton.LeftButton,pos=QPoint(80,70))
                    self.assertEqual(requests,[True])
                    self.assertTrue(window.isVisible())
                    window.hide()
        finally:
            image.hide()
            image.deleteLater()
            for window in windows[1:]:
                if hasattr(window, "stop_video"): self.video_cleanup(window)
                else: self.audio_cleanup(window)

    def test_52_mixed_navigation_keeps_one_folder_order_and_selection(self):
        from daemon import Controller
        from PyQt6.QtCore import QObject,pyqtSignal
        folder=self.home/"mixed-navigation"
        folder.mkdir()
        first,second,third,fourth=(folder/name for name in ("01.png","02.mp3","03.mp4","04.png"))
        shutil.copy2(self.first,first)
        shutil.copy2(ROOT/"examples/audio/preview.mp3",second)
        shutil.copy2(ROOT/"examples/video/preview.mp4",third)
        shutil.copy2(self.second,fourth)
        committed=[]
        class Order(QObject):
            idle=pyqtSignal()
            def __init__(self):
                super().__init__()
                self.paths=[str(p) for p in (first,second,third,fourth)]
                self.path=self.selected=str(first)
                self.process=None
                self.error=""
            def select(self,path): self.path=self.selected=path
            def finish(self,path):
                committed.append(path)
                self.idle.emit()
        controller=Controller(app)
        try:
            controller.request(json.dumps({"paths":[str(first)]}),False)
            self.audio_wait(lambda:controller.phase=="ready")
            order=Order()
            controller.image_selection=order
            for target,mode in ((second,"audio"),(third,"video"),(fourth,"image")):
                controller.navigate(1)
                self.assertEqual(controller.mode,mode)
                self.assertEqual(controller.path,str(target))
                self.assertIs(controller.active_order(),order)
                if mode=="image":self.audio_wait(lambda:controller.phase=="ready")
                if controller.audio:controller.audio.output.setVolume(0)
                if controller.video:controller.video.output.setVolume(0)
                self.assertEqual(order.selected,str(target))
                self.assertEqual(sum(bool(w and w.isVisible()) for w in
                    (controller.window,controller.audio,controller.video)),1)
                self.assertFalse(committed)
            controller.navigate(-1)
            self.assertEqual(controller.mode,"video")
            QTest.keyClick(controller.video,Qt.Key.Key_Space)
            self.assertEqual(committed,[str(third)])
            self.assertTrue(controller.video.player.source().isEmpty())
        finally:
            controller.close_view()
            controller.pool.waitForDone(5000)
            if controller.audio:self.audio_cleanup(controller.audio)
            if controller.video:self.video_cleanup(controller.video)
            controller.window.deleteLater()
            controller.deleteLater()

    def test_53_smb_url_bridge_and_dolphin_uri_encoding(self):
        from unittest.mock import patch
        from remote import resolve_input,folder_url,child_url,is_kio_fuse_path
        from PyQt6.QtCore import QUrl
        from manage import generated,PROBE,APP,RUNTIME
        mount="/run/user/1000/kio-fuse-test/smb/example.test/SampleShare/Папка"
        file=mount+"/99%20 # ?.jpg"
        remote="smb://example.test/SampleShare/Папка"
        url=child_url(QUrl(remote),Path(file).name)
        self.assertEqual(QUrl(url).fileName(),Path(file).name)
        self.assertIn("%2520",url)
        self.assertIn("%23",url)
        self.assertIn("%3F",url)
        with patch("remote.fuse_call",return_value=file) as call:
            self.assertEqual(resolve_input(url),file)
            self.assertEqual(call.call_args.args[0],"mountUrl")
        with patch("remote.fuse_call",return_value=remote):
            self.assertEqual(folder_url(file).toDisplayString(),remote)
        with patch("remote.fuse_call",side_effect=RuntimeError("offline")):
            with self.assertRaisesRegex(RuntimeError,"offline"):resolve_input(url)
        self.assertTrue(is_kio_fuse_path(file))
        self.assertFalse(is_kio_fuse_path(str(self.home/"kio-fuse-test/photo.jpg")))
        self.assertEqual(resolve_input(str(self.first)),str(self.first))
        for invalid in ("smb:filename.jpg","smb://router/share/a.jpg#fragment"):
            with self.assertRaises(ValueError):resolve_input(invalid)
        generated_files=generated()
        self.assertIn("X-KDE-Protocols=file,smb",generated_files[PROBE])
        self.assertIn(" toggle -- %U",generated_files[PROBE])
        self.assertIn(" open -- %U",generated_files[APP])
        self.assertIn("remote.py",RUNTIME)

    def test_54_cr2_navigation_mime_and_errors(self):
        import threading
        from unittest.mock import patch
        from core import EXTENSIONS, MEDIA_EXTENSIONS, neighbours, media_kind
        from manage import generated, PROBE, APP
        folder = self.home/"cr2-navigation"
        folder.mkdir()
        raw = folder/"02 — Canon.CR2"
        raw.write_bytes(b"broken Canon RAW")
        photo = folder/"01.jpg"
        shutil.copy2(self.rotated, photo)
        audio = folder/"03.mp3"
        audio.touch()
        self.assertIn(".cr2", EXTENSIONS)
        self.assertEqual(media_kind(raw), "image")
        self.assertEqual(neighbours(str(raw), threading.Event()), [str(photo), str(raw)])
        self.assertEqual(neighbours(str(raw), threading.Event(), MEDIA_EXTENSIONS),
                         [str(photo), str(raw), str(audio)])
        for target in (PROBE, APP):
            self.assertIn("image/x-canon-cr2;", generated()[target])
        with patch("loader.QImageReader.supportedImageFormats", return_value=[]):
            with self.assertRaisesRegex(ValueError, "kimageformats"):
                LoadJob(1, str(raw), []).decode()
        self.request("Open", raw)
        state = self.ready()
        self.assertTrue(state["error"])
        self.assertNotIn("Поддерживаются JPEG", state["error"])
        self.request("Open", photo)
        self.assertEqual(self.ready()["phase"], "ready")

    def test_55_wmv_flv_realmedia_playback_and_integration(self):
        import threading
        from core import VIDEO_EXTENSIONS, MEDIA_EXTENSIONS, media_kind, neighbours
        from manage import generated, PROBE, APP
        from PyQt6.QtMultimedia import QAudioBufferOutput
        for target in (PROBE, APP):
            for mime in ("video/x-ms-wmv;", "video/x-flv;", "application/vnd.rn-realmedia;"):
                self.assertIn(mime, generated()[target])
        window = self.video_window()
        buffers = []
        probe = QAudioBufferOutput(window)
        probe.audioBufferReceived.connect(lambda buffer: buffers.append(buffer.byteCount()))
        window.player.setAudioBufferOutput(probe)
        try:
            for extension in ("wmv", "flv", "rmvb"):
                with self.subTest(extension=extension):
                    self.assertIn("."+extension, VIDEO_EXTENSIONS)
                    path = ROOT/"examples/video"/("preview."+extension)
                    self.assertEqual(media_kind(str(path).upper()), "video")
                    self.assertIn(str(path), neighbours(str(path), threading.Event(), MEDIA_EXTENSIONS))
                    buffers.clear()
                    window.open_video(str(path))
                    self.audio_wait(lambda: window.phase == "error" or
                        (window.frames_received > 2 and any(buffers)), timeout=5000)
                    self.assertFalse(window.error, window.error)
                    self.assertTrue(window.snapshot()["has_frame"])
                    self.assertTrue(window.player.hasAudio())
                    self.assertGreater(window.player.duration(), 5000)
                    self.audio_wait(window.player.isSeekable)
                    window.player.setPosition(3000)
                    self.audio_wait(lambda: window.player.position() >= 2900)
                    window.stop_video()
                    self.assertTrue(window.player.source().isEmpty())
                    self.assertFalse(window.canvas.frame.isValid())
                    self.request("Open", path)
                    self.audio_wait(lambda: call("GetState").get("video", {}).get("frames_received", 0) > 2)
                    state = call("GetState")
                    self.assertEqual(state["module"], "video")
                    self.assertEqual(state["path"], str(path))
                    call("Close")
        finally:
            self.video_cleanup(window)

    def test_40_audio_uses_dolphin_order_and_commits_selection_on_close(self):
        from daemon import Controller
        from PyQt6.QtCore import QProcess,QObject,pyqtSignal
        folder = self.home/"audio-navigation"
        folder.mkdir()
        first,second,third = (folder/name for name in ("01.wav","02.mp3","10.flac"))
        for path,extension in ((first,"wav"),(second,"mp3"),(third,"flac")):
            shutil.copy2(ROOT/"examples/audio"/("preview."+extension),path)
        selected,finished = [],[]
        class Order(QObject):
            idle=pyqtSignal()
            def __init__(self):
                super().__init__()
                self.paths=[str(first),str(third),str(second)]  # Deliberately not name order.
                self.process=None
                self.error=""
                self.selected=str(first)
            def select(self,path):
                self.selected=path
                selected.append(path)
            def finish(self,path):
                finished.append(path)
                self.idle.emit()
        controller=Controller(app)
        try:
            controller.request(json.dumps({"paths":[str(first)]}),False)
            window=controller.audio
            window.output.setVolume(0)
            self.assertFalse(hasattr(controller,"audio_selection"))
            self.assertFalse(controller.findChildren(QProcess))
            order=Order()
            controller.audio_order=order
            controller.audio_order_ready(order)
            self.assertEqual(controller.path,str(first))  # Reading a snapshot never starts another track.
            window.present=lambda *_:self.fail("Navigation must not reactivate the viewer")
            QTest.keyClick(window,Qt.Key.Key_Right)
            self.assertEqual(controller.path,str(third))
            self.audio_wait(lambda:window.snapshot()["playing"])
            self.assertEqual(window.player.source().toLocalFile(),str(third))
            self.assertLess(window.player.position(),500)
            QTest.keyClick(window,Qt.Key.Key_Left)
            self.assertEqual(controller.path,str(first))
            controller.navigate(-1)
            self.assertEqual(controller.path,str(second))
            self.assertEqual(selected,[str(third),str(first),str(second)])
            second.unlink()
            controller.navigate(1)
            self.assertEqual(controller.path,str(second))
            QTest.keyClick(window,Qt.Key.Key_Space)
            self.assertFalse(window.isVisible())
            self.assertTrue(window.player.source().isEmpty())
            self.assertEqual(finished,[str(second)])
            self.assertIsNone(controller.audio_order)
        finally:
            controller.close_view()
            if controller.audio:self.audio_cleanup(controller.audio)
            controller.pool.waitForDone(5000)
            controller.window.deleteLater()
            controller.deleteLater()

    def test_41_explicit_dolphin_navigation_launcher_rollback(self):
        for custom in (False,True):
            with self.subTest(custom=custom):
                isolated=self.home/("dolphin-order-"+str(custom))
                env={**self.env,"HOME":str(isolated),"XDG_DATA_HOME":str(isolated/"data"),
                     "XDG_CONFIG_HOME":str(isolated/"config"),"XDG_STATE_HOME":str(isolated/"state")}
                launcher=isolated/"data/applications/org.kde.dolphin.desktop"
                original="[Desktop Entry]\nType=Application\nName=My Dolphin\nExec=dolphin --new-window %u\nIcon=folder\n"
                if custom:
                    launcher.parent.mkdir(parents=True)
                    launcher.write_text(original)
                for command in ("install", "update", "update"):
                    result=subprocess.run([sys.executable,"-B",str(ROOT/"manage.py"),command,"--isolated"],
                                          env=env,capture_output=True,text=True,timeout=15)
                    self.assertEqual(result.returncode,0,result.stderr)
                    content=launcher.read_text()
                    self.assertEqual(content.count("QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1"),1)
                    if custom:self.assertIn("dolphin --new-window %u",content)
                current=isolated/"data/kdeglance2/current"
                self.assertTrue((current/"dolphin_order_worker.py").exists())
                self.assertFalse((current/"audio_follow.py").exists())
                self.assertFalse((current/"dolphin_selection.py").exists())
                result=subprocess.run([sys.executable,"-B",str(ROOT/"manage.py"),"uninstall","--isolated","--purge"],
                                      env=env,capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(launcher.exists(),custom)
                if custom:self.assertEqual(launcher.read_text(),original)

    def test_42_audio_controls_do_not_navigate_and_volume_fades(self):
        navigation = []
        window = self.audio_window(navigation.append)
        try:
            window.open_audio(str(ROOT/"examples/audio/preview.wav"))
            self.audio_wait(lambda: window.snapshot()["playing"] and window.seek_bar.isEnabled())
            for widget in (window.play_button, window.speaker_button, window.elapsed,
                           window.total, window.controls, window.volume_panel, window.volume_slider):
                QTest.mouseClick(widget, Qt.MouseButton.LeftButton)
                self.assertTrue(window.isVisible())
            seek = window.seek_bar
            QTest.mousePress(seek, Qt.MouseButton.LeftButton, pos=QPoint(8,14))
            QTest.mouseRelease(seek, Qt.MouseButton.LeftButton, pos=QPoint(seek.width()+50,14))
            self.assertTrue(window.isVisible())
            self.assertFalse(navigation)
            window.set_volume(.5)
            for _ in range(60):
                smooth = QWheelEvent(QPointF(10,10), QPointF(10,10), QPoint(0,1), QPoint(),
                                     Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,
                                     Qt.ScrollPhase.ScrollUpdate,False)
                app.sendEvent(window.speaker_button, smooth)
            self.assertAlmostEqual(window.output.volume(), .525, places=5)
            window.hide_volume_panel()
            QTest.qWait(80)
            self.assertGreater(window.volume_opacity.opacity(), 0)
            self.assertLess(window.volume_opacity.opacity(), 1)
            window.set_volume(.65)  # A new change interrupts fading.
            self.assertEqual(window.volume_opacity.opacity(), 1)
            self.assertEqual(window.volume_slider.value(), 65)
            self.assertEqual(window.percent.text(), "65%")
            QTest.qWait(1850)
            self.assertFalse(window.volume_panel.isVisible())
            self.assertTrue(window.isVisible())
            self.assertFalse(navigation)
        finally:
            self.audio_cleanup(window)


if __name__ == "__main__":
    import faulthandler
    faulthandler.dump_traceback_later(75, repeat=True)
    if os.environ.get("QT_QPA_PLATFORM") != "offscreen":
        raise SystemExit("Use QT_QPA_PLATFORM=offscreen dbus-run-session -- python3 -B tests.py")
    from manage import validate_test_environment
    validate_test_environment()
    app = QApplication(["kdeglance-tests"])
    app.setQuitOnLastWindowClosed(False)
    unittest.main(verbosity=2)
