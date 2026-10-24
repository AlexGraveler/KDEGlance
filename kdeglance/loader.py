# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded image decoding entirely outside the GUI thread."""
from pathlib import Path
import threading
from collections import OrderedDict
from PyQt6.QtCore import QObject, QRunnable, QSize, Qt, pyqtSignal
from PyQt6.QtGui import QImageReader
from core import EXTENSIONS, MEDIA_EXTENSIONS, neighbours

MAX_EDGE = 4096
MAX_ANIMATION_BYTES = 192 * 1024 * 1024
MAX_FRAMES = 2000
HEIF_EXTENSIONS = {".heic", ".heif"}


class ImageCache:
    """Thread-safe, byte-bounded HEIF frames; never cache playlist snapshots."""
    def __init__(self, max_bytes=256*1024*1024, max_entries=6):
        self.max_bytes, self.max_entries = max_bytes, max_entries
        self.entries = OrderedDict()
        self.bytes = 0
        self.lock = threading.RLock()

    @staticmethod
    def signature(path):
        try:
            stat = Path(path).stat()
            return stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns
        except OSError:
            return None

    def get(self, path, signature):
        with self.lock:
            entry = self.entries.get(path)
            if entry is None:
                return None
            if signature is None or entry[0] != signature:
                self.bytes -= self.entries.pop(path)[2]
                return None
            self.entries.move_to_end(path)
            return dict(entry[1])

    def put(self, path, signature, result):
        if signature is None or len(result.get("frames", [])) != 1:
            return
        size = sum(image.sizeInBytes() for image, _ in result["frames"])
        if size > self.max_bytes:
            return
        payload = {key: result[key] for key in ("frames", "loops", "decoded_size")}
        with self.lock:
            if path in self.entries:
                self.bytes -= self.entries.pop(path)[2]
            self.entries[path] = signature, payload, size
            self.bytes += size
            while self.bytes > self.max_bytes or len(self.entries) > self.max_entries:
                _, evicted = self.entries.popitem(last=False)
                self.bytes -= evicted[2]

    def clear(self):
        with self.lock:
            self.entries.clear()
            self.bytes = 0


class Signals(QObject):
    done = pyqtSignal(int, object)


class LoadJob(QRunnable):
    def __init__(self, generation, path, selected, cache=None, mixed=False):
        super().__init__()
        self.setAutoDelete(False)
        self.generation, self.path, self.selected = generation, path, selected
        self.mixed = mixed
        self.cancelled = threading.Event()
        self.cache = cache if Path(path).suffix.lower() in HEIF_EXTENSIONS else None
        self.signals = Signals()

    def run(self):
        try:
            result = self.decode()
        except Exception as error:
            result = {"error": str(error), "frames": [], "playlist": [self.path]}
        self.signals.done.emit(self.generation, result)

    def decode(self):
        path = Path(self.path)
        if self.cancelled.is_set():
            return {"cancelled": True}
        if not path.is_file():
            raise ValueError("Файл не найден или недоступен")
        if path.suffix.lower() not in EXTENSIONS:
            raise ValueError("Поддерживаются JPEG, PNG, WebP, BMP, GIF, HEIC/HEIF и Canon CR2")
        is_cr2 = path.suffix.lower() == ".cr2"
        if is_cr2 and b"cr2" not in QImageReader.supportedImageFormats():
            raise ValueError("Для Canon CR2 необходим RAW-декодер Qt из пакета kimageformats")
        signature = self.cache.signature(path) if self.cache else None
        cached = self.cache.get(self.path, signature) if self.cache else None
        if cached is not None:
            playlist = self.selected if len(self.selected) > 1 else neighbours(self.path, self.cancelled, MEDIA_EXTENSIONS if self.mixed else EXTENSIONS)
            return {**cached, "error": "", "playlist": playlist,
                    "cache_hit": True, "cancelled": self.cancelled.is_set()}
        reader = QImageReader(str(path))
        reader.setAutoTransform(True)
        is_heif = path.suffix.lower() in HEIF_EXTENSIONS
        animated = reader.supportsAnimation()
        animation_count = reader.imageCount() if animated else 0
        size = QSize()
        # The installed HEIF plugin decodes during size(). PyQt's size() keeps
        # the GIL, unlike read(), so even a background size() stalls Python GUI
        # callbacks. Decode HEIF through read() first and bound it afterwards.
        # RAW size() reparses the camera file and its handler ignores ScaledSize.
        # Read once in the worker, then bound the resulting image as for HEIF.
        if not (is_heif or is_cr2):
            size = reader.size()
            if size.isValid() and max(size.width(), size.height()) > MAX_EDGE:
                reader.setScaledSize(size.scaled(QSize(MAX_EDGE, MAX_EDGE), Qt.AspectRatioMode.KeepAspectRatio))
        streaming = animated and (animation_count > MAX_FRAMES or
            (size.isValid() and min(size.width(), MAX_EDGE)*min(size.height(), MAX_EDGE)*4*animation_count > MAX_ANIMATION_BYTES))
        frames, memory = [], 0
        # Repeated read() advances animated image handlers; no QPixmap in workers.
        while not self.cancelled.is_set():
            image = reader.read()
            if image.isNull():
                raise ValueError("Не удалось прочитать изображение: " + reader.errorString())
            if (is_heif or is_cr2 or animated) and max(image.width(), image.height()) > MAX_EDGE:
                image = image.scaled(MAX_EDGE, MAX_EDGE, Qt.AspectRatioMode.KeepAspectRatio,
                                     Qt.TransformationMode.SmoothTransformation)
            # Treat image dimensions as physical pixels, including @2x filenames.
            image.setDevicePixelRatio(1.0)
            memory += image.sizeInBytes()
            if memory > MAX_ANIMATION_BYTES or len(frames) >= MAX_FRAMES:
                if animated and frames:
                    streaming = True
                    frames = frames[:1]
                    break
                raise ValueError("Изображение/анимация превышает лимит памяти или числа кадров")
            frames.append((image, max(20, reader.nextImageDelay())))
            if streaming:
                break
            if not reader.supportsAnimation() or not reader.canRead():
                break
        if self.cancelled.is_set():
            return {"cancelled": True}
        playlist = self.selected if len(self.selected) > 1 else neighbours(self.path, self.cancelled, MEDIA_EXTENSIONS if self.mixed else EXTENSIONS)
        result = {"frames": frames, "loops": reader.loopCount(), "playlist": playlist,
                  "error": "", "decoded_size": [frames[0][0].width(), frames[0][0].height()],
                  "cache_hit": False}
        if streaming:
            result['frames'] = frames[:1]
            result['animation_path'] = str(path)
            result['animation_frame_count'] = animation_count
        if (self.cache and not self.cancelled.is_set()
                and signature == self.cache.signature(path)):
            self.cache.put(self.path, signature, result)
        return result
