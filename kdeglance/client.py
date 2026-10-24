#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Short-lived Dolphin handler. Qt Core + D-Bus only; never creates a window."""
import argparse
import json
import os
import sys
import uuid
from PyQt6.QtCore import QCoreApplication
from PyQt6.QtDBus import QDBusConnection, QDBusMessage, QDBus
from core import BUS, OBJECT, INTERFACE, local_path, VERSION
from remote import resolve_input


def call(method, payload=None, timeout=10000):
    bus = QDBusConnection.sessionBus()
    message = QDBusMessage.createMethodCall(BUS, OBJECT, INTERFACE, method)
    if payload is not None:
        message.setArguments([json.dumps(payload, ensure_ascii=True)])
    reply = bus.call(message, QDBus.CallMode.Block, timeout)
    if reply.type() == QDBusMessage.MessageType.ErrorMessage:
        raise RuntimeError(reply.errorName() + ": " + reply.errorMessage())
    return json.loads(reply.arguments()[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="KDEGlance " + VERSION)
    parser.add_argument("command", choices=("open", "toggle", "close", "state"))
    parser.add_argument("paths", nargs="*")
    parser.add_argument("--request-id", default=None)
    parser.add_argument("--no-activate", action="store_true", help="Диагностика: не передавать activation token")
    args = parser.parse_args()
    token = os.environ.pop("XDG_ACTIVATION_TOKEN", "")
    app = QCoreApplication(["kdeglance2-client"])
    try:
        if args.command in ("open", "toggle"):
            if not args.paths:
                parser.error("нужен хотя бы один путь")
            # Read public KWin information before activation; no window mutations.
            msg = QDBusMessage.createMethodCall("org.kde.KWin", "/KWin", "org.kde.KWin", "activeOutputName")
            output = QDBusConnection.sessionBus().call(msg, QDBus.CallMode.Block, 100)
            screen = str(output.arguments()[0]) if output.type() == QDBusMessage.MessageType.ReplyMessage else ""
            payload = {"paths": [resolve_input(p) for p in args.paths],
                       "request_id": args.request_id or str(uuid.uuid4()),
                       "activation_token": "" if args.no_activate else token,
                       "screen_name": screen}
            result = call(args.command.capitalize(), payload)
        else:
            if args.paths:
                parser.error("команда не принимает пути")
            result = call("GetState" if args.command == "state" else "Close")
        print(json.dumps(result, ensure_ascii=True))
        return 0 if result.get("ok", True) else 1
    except (RuntimeError, ValueError, OSError) as error:
        print("KDEGlance: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
