# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded audio tags and cover decoding, exclusively in a worker thread."""
import base64
import os
import sys
import threading
from pathlib import Path
from PyQt6.QtCore import QObject, QRunnable, pyqtSignal, QBuffer, QByteArray, QIODevice, QSize, Qt
from PyQt6.QtGui import QImage, QImageReader

sys.path.insert(0, str(Path(__file__).with_name("vendor")))
import mutagen
from mutagen.flac import Picture

MAX_TAG_READ = 32*1024*1024
MAX_COVER_BYTES = 8*1024*1024
MAX_COVER_PIXELS = 16_000_000


class LimitedReader:
    def __init__(self, file):
        self.file, self.remaining = file, MAX_TAG_READ

    def read(self, size=-1):
        if size < 0:
            size = max(0, os.fstat(self.file.fileno()).st_size-self.file.tell())
        if size > self.remaining:
            raise ValueError("Лимит чтения метаданных")
        data = self.file.read(size)
        self.remaining -= len(data)
        return data

    def __getattr__(self, name):
        return getattr(self.file, name)


def cover_bytes(audio):
    pictures = getattr(audio, "pictures", [])
    if pictures:
        return next((p.data for p in pictures if p.type == 3), pictures[0].data)
    tags = audio.tags
    if not tags:
        return None
    if hasattr(tags, "getall"):
        pictures = tags.getall("APIC")
        if pictures:
            return next((p.data for p in pictures if p.type == 3), pictures[0].data)
    pictures = tags.get("covr", [])
    if pictures:
        return bytes(pictures[0])
    for value in tags.get("metadata_block_picture", []):
        if len(value) <= (MAX_COVER_BYTES+1024)*4//3:
            picture = Picture(base64.b64decode(value, validate=True))
            if picture.type == 3:
                return picture.data
    return None


def decode_cover(data):
    if not data or len(data) > MAX_COVER_BYTES:
        return QImage()
    buffer = QBuffer()
    buffer.setData(QByteArray(data))
    buffer.open(QIODevice.OpenModeFlag.ReadOnly)
    reader = QImageReader(buffer)
    # Covers are conventional raster images; do not invoke arbitrary plugins.
    if bytes(reader.format()).lower() not in {b"jpeg", b"jpg", b"png", b"webp", b"gif", b"bmp"}:
        return QImage()
    size = reader.size()
    if not size.isValid() or size.width()*size.height() > MAX_COVER_PIXELS:
        return QImage()
    reader.setAutoTransform(True)
    if max(size.width(), size.height()) > 1024:
        reader.setScaledSize(size.scaled(QSize(1024, 1024), Qt.AspectRatioMode.KeepAspectRatio))
    image = reader.read()
    if max(image.width(), image.height()) > 1024:
        image = image.scaled(1024, 1024, Qt.AspectRatioMode.KeepAspectRatio,
                             Qt.TransformationMode.SmoothTransformation)
    return image


class Signals(QObject):
    done = pyqtSignal(int, object)


class MetadataJob(QRunnable):
    def __init__(self, generation, path):
        super().__init__()
        self.setAutoDelete(False)
        self.generation, self.path = generation, path
        self.cancelled = threading.Event()
        self.signals = Signals()

    def run(self):
        result = {"cover": QImage(), "duration": None, "error": ""}
        try:
            if not self.cancelled.is_set():
                with open(self.path, "rb") as file:
                    audio = mutagen.File(LimitedReader(file))
                    if audio is not None:
                        result["duration"] = getattr(audio.info, "length", None)
                        result["cover"] = decode_cover(cover_bytes(audio))
        except Exception as error:
            result["error"] = str(error)
        if self.cancelled.is_set():
            result["cover"] = QImage()
        self.signals.done.emit(self.generation, result)
