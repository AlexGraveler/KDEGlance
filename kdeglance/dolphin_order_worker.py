#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One explicit Dolphin order/selection operation; no event subscriptions."""
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def execute(request):
    import gi
    gi.require_version("Gio", "2.0")
    from gi.repository import Gio, GLib
    from PyQt6.QtCore import QUrl
    from core import AUDIO_EXTENSIONS, EXTENSIONS, VIDEO_EXTENSIONS, MEDIA_EXTENSIONS
    from remote import folder_url, child_url
    extensions = {"image": EXTENSIONS, "video": VIDEO_EXTENSIONS, "all": MEDIA_EXTENSIONS}.get(request.get("media"), AUDIO_EXTENSIONS)
    flags = Gio.DBusCallFlags.NONE
    session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    address = session.call_sync("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress",
                                None, None, flags, 1000, None).unpack()[0]
    bus = Gio.DBusConnection.new_for_address_sync(address,
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
    accessible = "org.a11y.atspi.Accessible"
    selection = "org.a11y.atspi.Selection"

    def call(ref, interface, method, args=None):
        return bus.call_sync(ref[0], ref[1], interface, method, args, None, flags, 500, None).unpack()

    def prop(ref, interface, name):
        return call(ref, "org.freedesktop.DBus.Properties", "Get", GLib.Variant("(ss)",(interface,name)))[0]

    def name(ref):
        return prop(ref, accessible, "Name")

    def children(ref):
        return call(ref, accessible, "GetChildren")[0]

    def selected(ref):
        count = prop(ref, selection, "NSelectedChildren")
        if count != 1:
            return None
        return name(call(ref, selection, "GetSelectedChild", GLib.Variant("(i)",(0,)))[0])

    def select_current_item(context, current):
        # Dolphin's AT-SPI SelectChild changes selection only. openFiles also
        # sets the current item and keyboard anchor, without activateWindow.
        pid = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "GetConnectionUnixProcessID",
            GLib.Variant("(s)", (context["application"][0],)), None, flags, 1000, None).unpack()[0]
        service = "org.kde.dolphin-" + str(pid)
        interface = "org.kde.dolphin.MainWindow"

        def window_call(path, method, args=None, iface=interface):
            return session.call_sync(service, path, iface, method, args, None, flags, 1500, None).unpack()

        root = ET.fromstring(window_call("/dolphin", "Introspect", iface="org.freedesktop.DBus.Introspectable")[0])
        candidates = []
        url = child_url(source_directory, current.name)
        for node in root.findall("node"):
            path = "/dolphin/" + node.attrib["name"]
            xml = ET.fromstring(window_call(path, "Introspect", iface="org.freedesktop.DBus.Introspectable")[0])
            if any(i.attrib.get("name") == interface for i in xml.findall("interface")):
                if window_call(path, "isItemVisibleInAnyView", GLib.Variant("(s)", (url,)))[0]:
                    candidates.append(path)
        if len(candidates) != 1:
            raise RuntimeError("Не удалось однозначно найти исходное окно Dolphin для клавиатурного выбора")
        window_call(candidates[0], "openFiles", GLib.Variant("(asb)", ([url], False)))

    current = Path(request["path"]).absolute()
    source_directory = folder_url(current)
    context = request.get("context")
    if context is None:
        url = source_directory
        endings = tuple(part+tail for part in (url.toDisplayString(), url.toString(QUrl.ComponentFormattingOption.FullyEncoded)) for tail in ("","/"))
        candidates = []
        registry = ("org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root")
        for application in children(registry):
            if name(application).lower() != "dolphin":
                continue
            queue = [(application, 0)]
            visited = 0
            while queue and visited < 350:
                ref, depth = queue.pop(0)
                visited += 1
                role = call(ref, accessible, "GetRoleName")[0]
                if role in ("menu bar","tool bar","menu","popup menu","list item","table cell"):
                    continue
                if role in ("list","table","tree table"):
                    description = prop(ref, accessible, "Description")
                    if any(description.endswith(u) for u in endings) and selected(ref) == current.name:
                        candidates.append({"application": application, "view": ref, "directory": str(current.parent)})
                    continue
                if depth < 13:
                    queue.extend((child, depth+1) for child in children(ref)[:60])
        if len(candidates) != 1:
            raise RuntimeError("Не удалось однозначно получить порядок открытой папки Dolphin. Переоткройте Dolphin через ярлык.")
        context = candidates[0]
    if name(context["application"]).lower() != "dolphin" or context["directory"] != str(current.parent):
        raise RuntimeError("Исходная папка Dolphin изменилась")
    view = context["view"]
    url = source_directory
    description = prop(view,accessible,"Description")
    if not any(description.endswith(u) for u in
               tuple(part+tail for part in (url.toDisplayString(),url.toString(QUrl.ComponentFormattingOption.FullyEncoded)) for tail in ("","/"))):
        raise RuntimeError("Исходная папка Dolphin уже сменилась")
    entries = children(view)
    if len(entries) > 20000:
        raise RuntimeError("Слишком много элементов в папке Dolphin для одного просмотра")
    ordered = []
    indices = {}
    for index, child in enumerate(entries):
        filename = name(child)
        if (filename and Path(filename).name == filename
                and Path(filename).suffix.lower() in extensions):
            file = current.parent/filename
            if file.is_file():
                ordered.append(str(file))
                indices[str(file)] = index
    if str(current) not in indices:
        raise RuntimeError("Текущий файл больше не отображается в исходной папке Dolphin")
    if request.get("operation") == "select":
        # Do not replace a different, deliberate selection made by the user.
        allowed = {Path(p).name for p in request.get("allowed", [str(current)])}
        before = selected(view)
        if before not in allowed:
            raise RuntimeError("Выделение Dolphin изменено пользователем")
        select_current_item(context, current)
        if selected(view) != current.name:
            raise RuntimeError("Dolphin не подтвердил выделение звучащего файла")
    result = {"ok": True, "context": context, "paths": ordered, "selected": str(current)}
    # AT-SPI FOCUSED (12) is Dolphin's current item, independent of selection.
    state = call(entries[indices[str(current)]], accessible, "GetState")[0]
    result["current_item_matches"] = bool(state[0] & (1 << 12))
    if request.get("operation") == "select" and not result["current_item_matches"]:
        raise RuntimeError("Dolphin не подтвердил текущий файл для клавиатуры")
    return result


if __name__ == "__main__":
    try:
        request = json.loads(sys.stdin.readline(65536))
        response = execute(request)
    except Exception as error:
        response = {"ok": False, "error": str(error)[:400]}
    print(json.dumps(response, ensure_ascii=True), flush=True)
