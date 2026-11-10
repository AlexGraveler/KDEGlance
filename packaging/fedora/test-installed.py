#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Installed RPM smoke test on Plasma. Inject Qt input into a dedicated Dolphin.

Requires build/tests/libsessionadapter.so (BUILD_DESKTOP_TEST_ADAPTER=ON).
Does not simulate physical Wayland seat input or change MIME associations.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

root = Path(__file__).resolve().parents[2]
out = root / 'build/fedora-desktop'
out.mkdir(parents=True, exist_ok=True)
qdbus = shutil.which('qdbus6') or shutil.which('qdbus-qt6')
if not qdbus:
    raise SystemExit('Qt 6 D-Bus tools are required')


def call(service, path, method, *args):
    return subprocess.check_output([qdbus, service, path, method, *map(str, args)],
                                   text=True, stderr=subprocess.PIPE, timeout=15).strip()


def viewer(method, *args):
    return call('org.kde.kdeglance', '/Viewer', method, *args)


def status():
    return json.loads(viewer('Status'))


def wait(predicate, timeout=15):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        try:
            if predicate():
                return True
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            pass
        time.sleep(0.1)
    return False


def check(name, result):
    print(('PASS ' if result else 'FAIL ') + name, flush=True)
    if not result:
        raise RuntimeError(name)


check('Installed executable', Path('/usr/bin/kdeglance').is_file())
check('No existing preview is open', not status()['visible'])
check('D-Bus activation uses the installed systemd unit',
      subprocess.check_output(['systemctl', '--user', 'show', 'kdeglance.service',
                               '-p', 'ActiveState', '--value'], text=True).strip() == 'active')
check('KWin placement script loaded', status()['kwinScript'])
samples = out / 'samples'
samples.mkdir(exist_ok=True)
shutil.copy2(root / 'build/tests/format-samples/sample.png', samples / '01-image.png')
shutil.copy2(root / 'build/tests/format-samples/sample.jpg', samples / '02-image.jpg')
env = os.environ.copy()
env.pop('QT_PLUGIN_PATH', None)
env.pop('KDEGLANCE_KWIN_SCRIPT', None)
env['LD_PRELOAD'] = str(root / 'build/tests/libsessionadapter.so')
env['KDEGLANCE_LANG'] = 'en'
env['QT_QPA_PLATFORM'] = 'wayland'
with (out / 'dolphin.log').open('w') as log:
    dolphin = subprocess.Popen(['dolphin', '--new-window', '--select',
                                str(samples / '01-image.png')], env=env, stdout=log, stderr=log)
    adapter_service = f'org.kde.kdeglance.Test.p{dolphin.pid}'

    def adapter(method, *args):
        return call(adapter_service, '/KDEGlanceTest', method, *args)

    def bridge():
        return json.loads(call(f'org.kde.dolphin-{dolphin.pid}', '/KDEGlanceBridge', 'State'))

    try:
        check('Dedicated Dolphin adapter registered', wait(lambda: adapter('State')))
        check('Installed Dolphin hook loaded', wait(lambda: bridge()['modelAccess']))
        check('Dolphin selection access', bridge()['selectionAccess'])
        check('Focus file view', adapter('FocusFileView') == 'true')
        check('Send Space', adapter('Key', 'Space') == 'true')
        check('Space opens an image through the real hook', wait(lambda: status().get('hasImage')))
        check('Dolphin supplies both files', status()['count'] == 2)
        original = status()['url']
        viewer('Navigate', 1)
        check('Arrow navigation changes image', wait(lambda: status()['url'] != original))
        check('Selection follows the preview', wait(lambda: bridge()['target'] == status()['url']))
        viewer('Close')
        check('Preview closes', wait(lambda: not status()['visible']))
        adapter('FocusFileView')
        menu = subprocess.Popen([qdbus, adapter_service, '/KDEGlanceTest', 'Menu'],
                                stdout=log, stderr=log)
        try:
            check('Installed context menu action found', wait(lambda: adapter('QuickLook') == 'true'))
            check('Context menu opens image', wait(lambda: status().get('hasImage')))
        finally:
            menu.wait(timeout=10)
        viewer('Close')
    finally:
        try:
            adapter('Quit')
        finally:
            dolphin.wait(timeout=10)
            viewer('Quit')
