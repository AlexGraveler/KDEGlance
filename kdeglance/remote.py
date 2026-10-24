# SPDX-License-Identifier: GPL-3.0-or-later
"""SMB URL/local-path bridge through the user's existing KDE KIO Fuse service."""
import os
from pathlib import Path
from PyQt6.QtCore import QUrl, QCoreApplication
from PyQt6.QtDBus import QDBusConnection, QDBusMessage, QDBus
from core import local_path
_application = None


def is_kio_fuse_path(path):
    parts = Path(path).parts
    return any(part.startswith("kio-fuse-") for part in parts) and str(path).startswith("/run/user/")


def fuse_call(method, value, timeout=30000):
    global _application
    if QCoreApplication.instance() is None:
        _application = QCoreApplication(["kdeglance-remote"])
    message = QDBusMessage.createMethodCall("org.kde.KIOFuse", "/org/kde/KIOFuse", "org.kde.KIOFuse.VFS", method)
    message.setArguments([value])
    reply = QDBusConnection.sessionBus().call(message, QDBus.CallMode.Block, timeout)
    if reply.type() == QDBusMessage.MessageType.ErrorMessage:
        raise RuntimeError("KIO Fuse: " + reply.errorMessage())
    values = reply.arguments()
    if not values or not isinstance(values[0],str) or not values[0]:
        raise RuntimeError("KIO Fuse не вернул путь к сетевому файлу")
    return values[0]


def resolve_input(value):
    if value.lower().startswith("smb:"):
        url = QUrl(value)
        if not url.isValid() or url.scheme() != "smb" or not url.host() or not url.path().startswith("/"):
            raise ValueError("Некорректный адрес SMB")
        if url.hasQuery() or url.hasFragment():
            raise ValueError("Специальные символы в имени SMB-файла должны быть URL-кодированы")
        path = fuse_call("mountUrl",url.toString(QUrl.ComponentFormattingOption.FullyEncoded))
        if not os.path.isabs(path):
            raise RuntimeError("KIO Fuse вернул некорректный локальный путь")
        return local_path(path)
    return local_path(value)


def folder_url(path):
    """Original URL for Dolphin; decoders continue to use the local mount path."""
    directory = str(Path(path).absolute().parent)
    if is_kio_fuse_path(path):
        url = QUrl(fuse_call("remoteUrl",directory,5000))
        if not url.isValid() or url.scheme() != "smb" or not url.host():
            raise RuntimeError("Не удалось восстановить SMB-адрес исходной папки")
        return url.adjusted(QUrl.UrlFormattingOption.StripTrailingSlash)
    return QUrl.fromLocalFile(directory)


def child_url(directory_url, filename):
    url = QUrl(directory_url)
    url.setPath(url.path().rstrip("/")+"/"+filename)
    return url.toString(QUrl.ComponentFormattingOption.FullyEncoded)
