# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded animated-image decode ahead of playback, outside the GUI thread."""
from collections import deque
import threading
from PyQt6.QtCore import QRunnable, QSize, Qt
from PyQt6.QtGui import QImageReader


class AnimationStream(QRunnable):
    def __init__(self, path, first_size, loops, max_frames=3, max_bytes=32*1024*1024):
        super().__init__()
        self.setAutoDelete(True)
        self.path, self.size, self.loops = path, QSize(first_size), loops
        self.max_frames = max_frames
        self.max_bytes = max(max_bytes, first_size.width()*first_size.height()*4)
        self.condition = threading.Condition()
        self.queue = deque()
        self.bytes = self.peak_bytes = 0
        self.cancelled = False
        self.finished = False
        self.error = ''

    def cancel(self):
        with self.condition:
            self.cancelled = True
            self.queue.clear()
            self.bytes = 0
            self.condition.notify_all()

    def take(self):
        with self.condition:
            if not self.queue:
                return None
            item = self.queue.popleft()
            self.bytes -= item[0].sizeInBytes()
            self.condition.notify_all()
            return item

    def snapshot(self):
        with self.condition:
            return {'buffered_frames': len(self.queue), 'buffer_bytes': self.bytes,
                    'peak_buffer_bytes': self.peak_bytes, 'finished': self.finished,
                    'cancelled': self.cancelled, 'error': self.error}

    def run(self):
        try:
            iteration = 0
            while not self.cancelled:
                reader = QImageReader(self.path)
                reader.setAutoTransform(True)
                reader.setScaledSize(self.size)
                index = 0
                while not self.cancelled:
                    # Back pressure BEFORE decoding, not after accumulating an
                    # entire animation. Only one extra working frame may exist.
                    with self.condition:
                        while not self.cancelled and (len(self.queue) >= self.max_frames or self.bytes >= self.max_bytes):
                            self.condition.wait(.05)
                    if self.cancelled:
                        return
                    image = reader.read()
                    if image.isNull():
                        raise ValueError('Не удалось прочитать кадр анимации: '+reader.errorString())
                    if image.size() != self.size:
                        image = image.scaled(self.size, Qt.AspectRatioMode.KeepAspectRatio,
                                             Qt.TransformationMode.SmoothTransformation)
                    image.setDevicePixelRatio(1.0)
                    # The loader already displayed frame zero of iteration zero.
                    if index or iteration:
                        size = image.sizeInBytes()
                        with self.condition:
                            while not self.cancelled and self.queue and self.bytes+size > self.max_bytes:
                                self.condition.wait(.05)
                            if self.cancelled:
                                return
                            self.queue.append((image, max(20, reader.nextImageDelay()), index, iteration))
                            self.bytes += size
                            self.peak_bytes = max(self.peak_bytes, self.bytes)
                    index += 1
                    if not reader.canRead():
                        break
                iteration += 1
                if self.loops >= 0 and iteration > self.loops:
                    break
        except Exception as error:
            with self.condition:
                self.error = str(error)
        finally:
            with self.condition:
                self.finished = True
                self.condition.notify_all()
