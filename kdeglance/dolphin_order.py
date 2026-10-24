# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, asynchronous Dolphin snapshot and outgoing selection updates.

No timers poll selection and no event subscription can start playback.
"""
import json
from pathlib import Path
import sys
from PyQt6.QtCore import QObject, QProcess, QTimer, pyqtSignal


class DolphinOrder(QObject):
    ready = pyqtSignal()
    failed = pyqtSignal(str)
    idle = pyqtSignal()

    def __init__(self, path, parent=None, media="audio"):
        super().__init__(parent)
        self.media = media
        self.path = path
        self.selected = path
        self.context = None
        self.paths = []
        self.process = None
        self.pending = None
        self.finished_preview = False
        self.error = ""
        self._launch("snapshot", path)

    def select(self, path):
        self.path = path
        self.pending = path  # Coalesce rapid navigation to the latest track.
        self._pump()

    def finish(self, path):
        self.finished_preview = True
        self.select(path)

    def _pump(self):
        if self.process is not None:
            return
        if self.pending and self.context:
            path, self.pending = self.pending, None
            if path != self.selected:
                self._launch("select",path)
            elif self.finished_preview:
                self.idle.emit()
        elif self.finished_preview:
            self.idle.emit()

    def _launch(self, operation, path):
        process = QProcess(self)
        self.process = process
        process.setProgram(sys.executable)
        process.setArguments(["-B", str(Path(__file__).with_name("dolphin_order_worker.py"))])
        timer = QTimer(process)
        timer.setSingleShot(True)
        timer.timeout.connect(process.kill)
        timer.start(8000)

        def started():
            process.write((json.dumps({"operation":operation,"path":path,"context":self.context,"media":self.media,
                                       "allowed":[self.selected,path]},ensure_ascii=True)+"\n").encode())
            process.closeWriteChannel()

        def completed():
            if self.process is not process:
                return
            self.process = None
            timer.stop()
            data = bytes(process.readAllStandardOutput())
            try:
                result = json.loads(data) if len(data) < 8*1024*1024 else {}
            except (ValueError, UnicodeDecodeError):
                result = {}
            if result.get("ok"):
                self.error = ""
                self.context = result["context"]
                self.selected = result["selected"]
                self.paths = result["paths"]
                if operation == "snapshot":
                    if not self.finished_preview:
                        self.ready.emit()
            else:
                self.error = result.get("error", "Не удалось связаться с исходной папкой Dolphin")
                self.failed.emit(self.error)
                if operation == "snapshot":
                    self.pending = None
            process.deleteLater()
            self._pump()

        process.started.connect(started)
        process.finished.connect(completed)
        process.errorOccurred.connect(lambda error: completed() if error == QProcess.ProcessError.FailedToStart else None)
        process.readyReadStandardError.connect(lambda: process.readAllStandardError())
        process.start()
