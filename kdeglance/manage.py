#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""User-only installation, updates and reversible Dolphin integration."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

SOURCE = Path(__file__).resolve().parent
DATA = Path(os.environ.get("XDG_DATA_HOME") or Path.home()/".local/share")
CONFIG = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home()/".config")
STATE = Path(os.environ.get("XDG_STATE_HOME") or Path.home()/".local/state") / "kdeglance2"
ROOT = DATA / "kdeglance2"
CURRENT = ROOT / "current"
UNIT = CONFIG / "systemd/user/kdeglance2.service"
PROBE = DATA / "kio/servicemenus/kdeglance-space-probe.desktop"
APP = DATA / "applications/io.github.KDEGlance2.desktop"
ACTIVATION = DATA / "dbus-1/services/io.github.KDEGlance2.service"
BIN = Path.home()/".local/bin/kdeglance2"
PUBLIC_BIN = Path.home()/".local/bin/kdeglance"
MANAGER = Path.home()/".local/bin/kdeglance-manage"
MANIFEST = STATE / "installation.json"
RUNTIME = ("core.py", "loader.py", "window.py", "daemon.py", "client.py", "libkdeglance_native.so",
           "manage.py", "build_native.py", "native.cpp", "README.md", "audio.py", "audio_metadata.py", "vendor", "dolphin_order.py", "dolphin_order_worker.py", "video.py", "interactions.py", "remote.py", "animation.py", "visuals.py", "COPYING", "THIRD_PARTY.md")


def dolphin_launcher():
    """Enable explicit order/selection requests for Dolphin only; no observer."""
    destination = DATA/"applications/org.kde.dolphin.desktop"
    source = destination if destination.exists() else Path("/usr/share/applications/org.kde.dolphin.desktop")
    if not source.is_file():
        return {}
    result, main = [], False
    prefix = "/usr/bin/env QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 "
    for line in source.read_text().splitlines():
        if line.startswith("["):
            main = line == "[Desktop Entry]"
        if main and line.startswith("Exec=") and "QT_LINUX_ACCESSIBILITY_ALWAYS_ON=" not in line:
            line = "Exec=" + prefix + line[5:]
        if main and line == "DBusActivatable=true":
            line = "DBusActivatable=false"
        result.append(line)
    return {destination: "\n".join(result)+"\n"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atomic(path, text, mode=0o600):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".kdeglance2-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            stream.write(text)
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def systemctl(*args, check=True):
    return subprocess.run(["systemctl", "--user", *args], check=check)


def reload_activation(check=True):
    # The session bus may not watch a service directory created after login.
    # Refresh activation metadata; do not edit/restart the bus itself.
    return subprocess.run(["busctl", "--user", "call", "org.freedesktop.DBus",
                           "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReloadConfig"], check=check)


def quote(value):
    # Desktop Entry quoting is not shell quoting.
    value = str(value).replace("\\", "\\\\\\\\").replace('"', '\\"').replace("`", "\\`").replace("$", "\\$")
    return '"' + value.replace("%", "%%") + '"'


def generated():
    exec_client = "/usr/bin/python3 -B " + quote(CURRENT/"client.py")
    mime = "image/jpeg;image/png;image/webp;image/bmp;image/gif;image/heif;image/heic;image/x-canon-cr2;"
    mime += "audio/mpeg;audio/flac;audio/x-flac;audio/ogg;audio/x-vorbis+ogg;application/ogg;audio/wav;audio/x-wav;audio/mp4;audio/x-m4a;audio/aac;audio/x-aac;"
    mime += "video/mp4;video/x-matroska;video/webm;video/x-msvideo;video/quicktime;video/x-m4v;video/mpeg;video/mp2t;video/ogg;video/3gpp;"
    mime += "video/x-ms-wmv;video/x-flv;application/vnd.rn-realmedia;video/vnd.rn-realvideo;"
    return {**dolphin_launcher(),
        BIN: "#!/bin/sh\nexec /usr/bin/python3 -B " + shlex.quote(str(CURRENT/"client.py")) + ' "$@"\n',
        PUBLIC_BIN: "#!/bin/sh\nexec /usr/bin/python3 -B " + shlex.quote(str(CURRENT/"client.py")) + ' "$@"\n',
        MANAGER: "#!/bin/sh\nexec /usr/bin/python3 -B " + shlex.quote(str(CURRENT/"manage.py")) + ' "$@"\n',
        UNIT: "[Unit]\nDescription=KDEGlance image preview\nPartOf=graphical-session.target\n"
              "After=graphical-session-pre.target\nStartLimitIntervalSec=60\nStartLimitBurst=5\n\n"
              "[Service]\nType=dbus\nBusName=io.github.KDEGlance2\n"
              'ExecStart=/usr/bin/python3 -B "' + str(CURRENT/"daemon.py").replace("%", "%%") + '"\n'
              "Environment=QT_QPA_PLATFORM=wayland\nUMask=0077\nRestart=on-failure\nRestartSec=1\n"
              "TimeoutStopSec=10\n\n[Install]\nWantedBy=graphical-session.target\n",
        ACTIVATION: "[D-BUS Service]\nName=io.github.KDEGlance2\nExec=/usr/bin/python3 -B "
                    + quote(CURRENT/"daemon.py") + "\nSystemdService=kdeglance2.service\n",
        APP: "[Desktop Entry]\nType=Application\nName=KDEGlance\nIcon=image-viewer\n"
             "Exec=" + exec_client + " open -- %U\nMimeType=" + mime +
             "\nTerminal=false\nNoDisplay=true\nStartupNotify=true\nCategories=Graphics;Viewer;\n",
        PROBE: "[Desktop Entry]\nType=Service\nName=KDEGlance\nX-KDE-AliasFor=io.github.KDEGlance2\nMimeType=" + mime +
               "\nActions=LogSelection;\nX-KDE-Protocols=file,smb\nX-KDE-RequiredNumberOfUrls=1\n\n"
               "[Desktop Action LogSelection]\nName=KDEGlance: Quick Look\n"
               "Name[ru]=KDEGlance: быстрый просмотр\nIcon=image-viewer\nExec="
               + exec_client + " toggle -- %U\n",
    }


def snapshot(paths):
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    backup = STATE / "backups" / stamp
    backup.mkdir(parents=True, mode=0o700)
    entries = {}
    for i, path in enumerate(dict.fromkeys(paths)):
        if path.is_symlink():
            raise RuntimeError("Не заменяем чужую символическую ссылку: " + str(path))
        item = {"existed": path.exists(), "copy": None}
        if path.exists():
            if not path.is_file():
                raise RuntimeError("Ожидался обычный файл: " + str(path))
            destination = backup / str(i)
            shutil.copy2(path, destination)
            destination.chmod(0o600)
            item.update(copy=str(destination), sha256=digest(destination), mode=path.stat().st_mode & 0o777)
        entries[str(path)] = item
    atomic(backup/"manifest.json", json.dumps(entries, ensure_ascii=False, indent=2))
    return entries, str(backup)


def restore(path, original):
    if original["existed"]:
        atomic(path, Path(original["copy"]).read_text(), original["mode"])
    elif path.exists():
        path.unlink()


def validate_test_environment():
    value = os.environ.get("KDEGLANCE_TEST_ROOT", "")
    root = Path(value).resolve() if value else None
    if not root or not root.is_absolute() or not (root/".kdeglance-test-root").is_file():
        raise RuntimeError("--isolated requires a marked disposable KDEGLANCE_TEST_ROOT")
    import pwd
    if Path.home().resolve() == Path(pwd.getpwuid(os.getuid()).pw_dir).resolve():
        raise RuntimeError("--isolated refuses the real account home")
    for value in (Path.home(), DATA, CONFIG, STATE, Path(os.environ.get("XDG_RUNTIME_DIR", "/"))):
        if not value.resolve().is_relative_to(root):
            raise RuntimeError("All test HOME/XDG paths must stay inside KDEGLANCE_TEST_ROOT")


def validate_root():
    if ROOT.is_symlink() or STATE.is_symlink():
        raise RuntimeError("Refusing a symlinked application/state root")
    if CURRENT.is_symlink() and not CURRENT.resolve().is_relative_to((ROOT/"releases").resolve()):
        raise RuntimeError("Current runtime points outside the owned releases directory")
    if ROOT.exists() and (not (ROOT/".owned-by-kdeglance2").is_file() or (ROOT/".owned-by-kdeglance2").is_symlink()):
        raise RuntimeError("Application directory is not owned by this installer")


def load_manifest():
    manifest = json.loads(MANIFEST.read_text())
    allowed = {UNIT, PROBE, APP, ACTIVATION, BIN, PUBLIC_BIN, MANAGER,
               DATA/"applications/org.kde.dolphin.desktop"}
    validate_root()
    if manifest.get("runtime") != str(ROOT):
        raise RuntimeError("Unexpected installation root in manifest")
    for name, expected in manifest["installed"].items():
        path = Path(name)
        if path not in allowed or path.is_symlink():
            raise RuntimeError("Refusing an unowned/symlinked integration path")
        original = manifest["original"][name]
        if original["existed"]:
            copy = Path(original["copy"])
            if not copy.resolve().is_relative_to((STATE/"backups").resolve()) or not copy.is_file() or copy.is_symlink():
                raise RuntimeError("Invalid original-file backup")
            if digest(copy) != original["sha256"]:
                raise RuntimeError("Original-file backup checksum mismatch")
    return manifest


def install(isolated=False):
    from core import VERSION
    validate_root()
    if ROOT.exists() and not (ROOT/".owned-by-kdeglance2").exists():
        raise RuntimeError("Каталог установки существует и не принадлежит этому установщику")
    missing = [name for name in RUNTIME if not (SOURCE/name).exists()]
    if missing:
        raise RuntimeError("Missing runtime files: " + ", ".join(missing))
    if not (SOURCE/"libkdeglance_native.so").is_file():
        raise RuntimeError("Сначала выполните python3 build_native.py; системные пакеты автоматически не устанавливаются")
    files = generated()
    configs = [CONFIG/n for n in ("dolphinrc", "kglobalshortcutsrc", "kservicemenurc")]
    configs += [DATA/n for n in ("kxmlgui5/dolphin/dolphinui.rc", "kxmlgui6/dolphin/dolphinui.rc", "dolphin/dolphinui.rc")]
    previous = load_manifest() if MANIFEST.exists() else None
    for path in files:
        if path.is_symlink():
            raise RuntimeError("Refusing to overwrite a symlink: " + str(path))
        if path.exists() and path != DATA/"applications/org.kde.dolphin.desktop" and (not previous or str(path) not in previous["installed"]):
            raise RuntimeError("Existing file is not owned by KDEGlance: " + str(path))
    entries, backup = snapshot([*files,*configs])
    baseline = dict(previous["original"]) if previous else {}
    for path in files:
        baseline.setdefault(str(path), entries[str(path)])
    ROOT.mkdir(parents=True, exist_ok=True, mode=0o700)
    atomic(ROOT/".owned-by-kdeglance2", "KDEGlance\n")
    release = ROOT/"releases"/(VERSION + "-" + str(time.time_ns()))
    release.mkdir(parents=True)
    for name in RUNTIME:
        if (SOURCE/name).is_dir():
            shutil.copytree(SOURCE/name, release/name, ignore=shutil.ignore_patterns("__pycache__"))
        else:
            shutil.copy2(SOURCE/name, release/name)
    old_release = os.readlink(CURRENT) if CURRENT.is_symlink() else None
    if CURRENT.exists() and not CURRENT.is_symlink():
        raise RuntimeError("current должен быть ссылкой установщика")
    link = ROOT/(".current-" + str(time.time_ns()))
    link.symlink_to(release)
    os.replace(link, CURRENT)
    changed = []
    try:
        # Keep the proven probe untouched until the daemon answers GetState.
        for path, content in files.items():
            if path == PROBE:
                continue
            atomic(path, content, 0o700 if path in (BIN, PUBLIC_BIN, MANAGER) else 0o600)
            changed.append(path)
        if not isolated:
            reload_activation()
            systemctl("daemon-reload")
            if not previous:
                systemctl("enable", "kdeglance2.service")
            systemctl("restart", "kdeglance2.service")
            for attempt in range(20):
                result = subprocess.run([sys.executable, "-B", str(CURRENT/"client.py"), "state"],
                                        capture_output=True, text=True, timeout=12)
                if result.returncode == 0 and json.loads(result.stdout).get("version") == VERSION:
                    break
                time.sleep(0.15)
            else:
                raise RuntimeError("Новый процесс не отвечает: " + result.stderr)
        atomic(PROBE, files[PROBE], 0o700)
        changed.append(PROBE)
        installed = {str(p): digest(p) for p in files}
        atomic(MANIFEST, json.dumps({"version": VERSION, "original": baseline, "installed": installed,
                                   "latest_backup": backup, "runtime": str(ROOT)}, ensure_ascii=False, indent=2))
    except Exception:
        for path in reversed(changed):
            restore(path, entries[str(path)])
        if old_release:
            CURRENT.unlink()
            CURRENT.symlink_to(old_release)
        elif CURRENT.is_symlink():
            CURRENT.unlink()
        if not isolated:
            systemctl("stop", "kdeglance2.service", check=False)
            if not previous:
                systemctl("disable", "kdeglance2.service", check=False)
            systemctl("daemon-reload", check=False)
            reload_activation(check=False)
            if previous:
                systemctl("start", "kdeglance2.service", check=False)
        raise
    print(json.dumps({"installed": str(ROOT), "backup": backup, "integration": str(PROBE),
                      "shortcut_settings_modified": False, "dolphin_explicit_navigation": DATA/"applications/org.kde.dolphin.desktop" in files, "isolated": isolated}, ensure_ascii=False, indent=2))


def rollback_integration():
    manifest = load_manifest()
    expected = manifest["installed"].get(str(PROBE))
    if PROBE.exists() and digest(PROBE) != expected:
        raise RuntimeError("Действие изменено после установки; сохраните и проверьте его вручную")
    snapshot([PROBE])
    restore(PROBE, manifest["original"][str(PROBE)])
    manifest["installed"].pop(str(PROBE), None)
    atomic(MANIFEST, json.dumps(manifest, ensure_ascii=False, indent=2))
    print("Исходный тестовый обработчик восстановлен; эксперимент и Space сохранены")


def restore_dolphin():
    manifest = load_manifest()
    path = DATA/"applications/org.kde.dolphin.desktop"
    expected = manifest["installed"].get(str(path))
    if expected is None:
        print("Ярлык Dolphin уже восстановлен или не менялся")
        return
    if path.exists() and digest(path) != expected:
        raise RuntimeError("Ярлык Dolphin изменён пользователем; автоматическое восстановление остановлено")
    snapshot([path])
    restore(path, manifest["original"][str(path)])
    manifest["installed"].pop(str(path), None)
    atomic(MANIFEST, json.dumps(manifest, ensure_ascii=False, indent=2))
    print("Исходный пользовательский ярлык Dolphin восстановлен")


def uninstall(isolated=False, purge=False):
    if not MANIFEST.exists():
        print("No KDEGlance installation manifest; nothing removed")
        return
    manifest = load_manifest()
    for name, expected in manifest["installed"].items():
        path = Path(name)
        if path.exists() and digest(path) != expected:
            raise RuntimeError("Файл изменён пользователем; удаление остановлено: " + name)
    if not isolated:
        systemctl("disable", "--now", "kdeglance2.service", check=False)
    for name in manifest["installed"]:
        restore(Path(name), manifest["original"][name])
    if (ROOT/".owned-by-kdeglance2").is_file():
        shutil.rmtree(ROOT)
    if not isolated:
        systemctl("daemon-reload")
        reload_activation()
        subprocess.run(["systemctl", "--user", "reset-failed", "kdeglance2.service"], check=False)
    if purge:
        shutil.rmtree(STATE)
    else:
        MANIFEST.unlink()
    print("KDEGlance удалён; исходное Service Menu восстановлено. "
          + ("Логи и снимки удалены." if purge else "Логи и резервные копии сохранены: " + str(STATE)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("install", "update", "uninstall", "rollback-integration", "restore-dolphin", "disable-autostart", "enable-autostart", "status", "logs"))
    parser.add_argument("--isolated", action="store_true", help="Only for disposable XDG/HOME tests: do not contact systemd")
    parser.add_argument("--purge", action="store_true", help="Also remove KDEGlance logs/backups on uninstall")
    args = parser.parse_args()
    if args.isolated:
        try:
            validate_test_environment()
        except RuntimeError as error:
            parser.error(str(error))
    os.umask(0o077)
    try:
        if args.command in ("install", "update"):
            install(args.isolated)
        elif args.command == "uninstall":
            if args.purge and not args.isolated:
                print("This also deletes KDEGlance logs and installer backups.")
                if not sys.stdin.isatty() or input("Continue? [y/N] ").lower() not in ("y", "yes"):
                    raise RuntimeError("Purge cancelled")
            uninstall(args.isolated, args.purge)
        elif args.command == "rollback-integration":
            rollback_integration()
        elif args.command == "restore-dolphin":
            restore_dolphin()
        elif args.command == "disable-autostart":
            systemctl("disable", "--now", "kdeglance2.service")
            print("Автозапуск отключён; открытие через Dolphin по-прежнему может активировать D-Bus-сервис")
        elif args.command == "enable-autostart":
            systemctl("enable", "--now", "kdeglance2.service")
        elif args.command == "status":
            systemctl("status", "kdeglance2.service", check=False)
        elif args.command == "logs":
            subprocess.run(["journalctl", "--user", "-u", "kdeglance2.service", "-n", "80", "--no-pager"], check=False)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print("KDEGlance: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
