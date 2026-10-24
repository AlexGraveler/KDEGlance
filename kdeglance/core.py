# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared protocol and lossless local-path handling (no GUI required)."""
import os
from pathlib import Path
import re
from PyQt6.QtCore import QUrl

VERSION = "1.0.0"
BUS = "io.github.KDEGlance2"
OBJECT = "/io/github/KDEGlance2/Preview"
INTERFACE = BUS + ".Preview1"
EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".gif", ".heic", ".heif", ".cr2"}
AUDIO_EXTENSIONS = {".mp3", ".flac", ".ogg", ".wav", ".m4a", ".aac"}
VIDEO_EXTENSIONS = {".mp4", ".mkv", ".webm", ".avi", ".mov", ".m4v", ".mpeg", ".mpg", ".ts", ".mts", ".m2ts", ".ogv", ".3gp", ".wmv", ".flv", ".rmvb"}
MEDIA_EXTENSIONS = EXTENSIONS | AUDIO_EXTENSIONS | VIDEO_EXTENSIONS


def media_kind(path):
    if Path(path).suffix.lower() in VIDEO_EXTENSIONS:
        return "video"
    return "audio" if Path(path).suffix.lower() in AUDIO_EXTENSIONS else "image"


def local_path(value):
    if not value or "\0" in value:
        raise ValueError("Пустой путь или недопустимый нулевой символ")
    if value.startswith("file:"):
        url = QUrl(value)
        if not url.isLocalFile() or url.host() not in ("", "localhost"):
            raise ValueError("Поддерживаются только локальные файлы")
        value = url.toLocalFile()
    elif not os.path.isabs(value) and "://" in value:
        raise ValueError("Поддерживаются только локальные файлы")
    # Literal percent signs and leading/trailing whitespace are preserved.
    return os.path.abspath(value)


def natural_key(path):
    return tuple((1, int(part)) if part.isdigit() else (0, part.casefold())
                 for part in re.split(r"(\d+)", Path(path).name))


def neighbours(path, cancelled, extensions=EXTENSIONS):
    result = []
    try:
        with os.scandir(os.path.dirname(path)) as entries:
            for entry in entries:
                if cancelled.is_set():
                    return [path]
                if Path(entry.name).suffix.lower() in extensions and entry.is_file():
                    result.append(entry.path)
    except OSError:
        return [path]
    result.sort(key=natural_key)
    return result if path in result else [path]

