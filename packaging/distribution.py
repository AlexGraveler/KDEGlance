# SPDX-License-Identifier: GPL-3.0-or-later
"""Allowlist for the user source distribution; never include repository tests/media."""
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
APP_FILES=(
    'core.py','loader.py','window.py','daemon.py','client.py','manage.py',
    'build_native.py','native.cpp','README.md','audio.py','audio_metadata.py',
    'dolphin_order.py','dolphin_order_worker.py','video.py','interactions.py',
    'remote.py','animation.py','visuals.py','COPYING','THIRD_PARTY.md',
)
INSTALL_FILES=('install.py','uninstall.py','check_env.py')

def archive_files():
    result={name:ROOT/name for name in ('install.sh','uninstall.sh','VERSION','LICENSE','THIRD_PARTY.md')}
    result['README.md']=ROOT/'packaging/user-README.md'
    for name in APP_FILES:result['kdeglance/'+name]=ROOT/'kdeglance'/name
    for name in INSTALL_FILES:result['packaging/'+name]=ROOT/'packaging'/name
    vendor=ROOT/'kdeglance/vendor'
    for path in sorted(vendor.rglob('*')):
        if '__pycache__' in path.parts or path.suffix in ('.pyc','.pyo'):continue
        if path.is_symlink():raise RuntimeError('Unexpected vendored symlink: '+str(path))
        if path.is_file():result[path.relative_to(ROOT).as_posix()]=path
    for name,path in result.items():
        if not path.is_file() or path.is_symlink():raise RuntimeError('Missing/non-regular distribution file: '+name)
    return dict(sorted(result.items()))
