# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only Arch/CachyOS environment and dependency checks."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent
PACKAGES=('python','python-pyqt6','python-gobject','qt6-base','qt6-multimedia','qt6-wayland',
          'qt6-imageformats','kimageformats','kwindowsystem','kservice','dolphin','kio-fuse',
          'kio-extras','at-spi2-core','glib2','dbus','systemd','gcc','pkgconf','plasma-workspace')
_app=None

def run(args):
    return subprocess.run(args,capture_output=True,text=True,timeout=15)

def os_info(path=Path('/etc/os-release')):
    data={}
    for line in path.read_text().splitlines():
        if '=' in line:
            key,value=line.split('=',1);data[key]=value.strip().strip('"')
    return data

def version(value):
    return tuple(int(n) for n in re.findall(r'\d+',value)[:3])

def probe(require_session=True, release_file=Path('/etc/os-release')):
    global _app
    result={'missing':[],'errors':[],'info':{},'warnings':[],'supported':True}
    info=os_info(release_file)
    result['info']['distribution']=info.get('PRETTY_NAME',info.get('ID','unknown'))
    if info.get('ID') not in ('arch','cachyos') and 'arch' not in info.get('ID_LIKE','').split():
        result['supported']=False
        result['errors'].append('Only Arch Linux/CachyOS are supported by this installer.')
    if not shutil.which('pacman'):
        result['errors'].append('pacman is not available.');return result
    check=run(['pacman','-T',*PACKAGES])
    if check.returncode in (0,127):result['missing']=check.stdout.splitlines()
    else:result['errors'].append('pacman dependency check failed: '+check.stderr.strip())
    for package,minimum in [('dolphin',(24,2)),('plasma-workspace',(6,0))]:
        check=run(['pacman','-Q',package])
        if not check.returncode:
            package_version=check.stdout.split()[-1].split(':')[-1]
            result['info'][package]=package_version
            if version(package_version)<minimum:result['errors'].append(package+' is too old.')
    if sys.version_info<(3,10):result['errors'].append('Python 3.10 or newer is required.')
    result['info']['python']=sys.version.split()[0]
    if result['missing']:
        result['warnings'].append('Runtime import checks deferred until missing packages are installed.')
    else:
        try:
            from PyQt6.QtCore import QCoreApplication,QT_VERSION_STR,PYQT_VERSION_STR
            from PyQt6.QtDBus import QDBusConnection
            from PyQt6.QtGui import QImageReader
            from PyQt6.QtMultimedia import QMediaPlayer
            import gi
            gi.require_version('Gio','2.0')
            from gi.repository import Gio,GLib
            _app=QCoreApplication.instance() or QCoreApplication(['kdeglance-check'])
            result['info'].update(qt=QT_VERSION_STR,pyqt=PYQT_VERSION_STR)
            if version(QT_VERSION_STR)<(6,8) or version(PYQT_VERSION_STR)<(6,8):result['errors'].append('Qt/PyQt 6.8 or newer is required.')
            result['info']['image_formats']=sorted(bytes(x).decode() for x in QImageReader.supportedImageFormats())
            if require_session and not QDBusConnection.sessionBus().isConnected():result['errors'].append('No graphical user D-Bus session is available.')
        except (ImportError,ValueError) as error:result['errors'].append('Python/Qt/Gio check: '+str(error))
    if require_session:
        if 'kde' not in os.environ.get('XDG_CURRENT_DESKTOP','').lower().split(':'):result['errors'].append('Run this installer in a KDE Plasma desktop session.')
        if os.environ.get('XDG_SESSION_TYPE')!='wayland' and not os.environ.get('WAYLAND_DISPLAY'):result['errors'].append('A Plasma Wayland session is required for the supported configuration.')
        try:
            check=run(['systemctl','--user','show-environment'])
            if check.returncode:result['errors'].append('The user systemd manager is unavailable.')
        except OSError:result['errors'].append('systemctl is unavailable.')
    if not result['missing']:
        try:
            check=run(['pkg-config','--exists','Qt6Gui >= 6.8','KF6WindowSystem >= 6.0'])
            if check.returncode:result['errors'].append('Qt/KF6 build metadata is missing or too old.')
        except OSError:result['errors'].append('pkg-config is unavailable.')
    return result

def show(result):
    print('KDEGlance 1.0.1 — Arch Linux / CachyOS')
    for key,value in result['info'].items():print(str(key)+': '+str(value))
    if result['missing']:print('Missing packages: '+' '.join(result['missing']))
    for error in result['errors']:print('ERROR: '+error)
    for warning in result['warnings']:print('NOTE: '+warning)
