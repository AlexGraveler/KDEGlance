#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Explicit, reversible Dolphin accessibility setup; never change shortcuts."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import sys
import tempfile
from package_support import check_manual,check_command_path,locations
from legacy import archive_experimental_menu

SYSTEM_DOLPHIN=Path('/usr/share/applications/org.kde.dolphin.desktop')

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def atomic(path,data,mode=0o600):
    path.parent.mkdir(parents=True,exist_ok=True)
    descriptor,name=tempfile.mkstemp(prefix='.kdeglance-',dir=path.parent)
    try:
        with os.fdopen(descriptor,'wb') as stream:stream.write(data)
        os.chmod(name,mode);os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)

def paths():
    _,data,_,state=locations()
    directory=state/'kdeglance-aur'
    return data/'applications/org.kde.dolphin.desktop',directory/'dolphin-setup.json',directory/'dolphin-original.desktop'

def validate(target,manifest,backup):
    for path in (target,manifest,backup,manifest.parent):
        if path.is_symlink():raise RuntimeError('Refusing a symlink: '+str(path))
    if manifest.exists():
        saved=json.loads(manifest.read_text())
        if saved['target']!=str(target):raise RuntimeError('Unexpected setup target')
        if not target.is_file() or digest(target)!=saved['installed_sha256']:
            raise RuntimeError('Dolphin launcher changed; refusing to overwrite it. Preserve/review it manually.')
        if saved['existed'] and (not backup.is_file() or digest(backup)!=saved['original_sha256']):
            raise RuntimeError('Original launcher backup is missing or changed')
        return saved
    return None

def enable():
    check_manual()
    target,manifest,backup=paths();saved=validate(target,manifest,backup)
    if saved:
        print('Dolphin accessibility launcher already configured.');return
    if backup.exists():raise RuntimeError('Unowned setup backup exists; refusing to overwrite')
    source=target if target.exists() else SYSTEM_DOLPHIN
    if not source.is_file():raise RuntimeError('Dolphin launcher is missing')
    text=source.read_text();lines=[];main=False;found=False
    for line in text.splitlines():
        if line.startswith('['):main=line=='[Desktop Entry]'
        if main and line.startswith('Exec='):
            found=True
            if 'QT_LINUX_ACCESSIBILITY_ALWAYS_ON=' in line and not re.search(r'(?:^|\s)QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1(?:\s|$)',line[5:]):
                raise RuntimeError('Existing accessibility assignment is not enabled; review the custom launcher manually')
            if 'QT_LINUX_ACCESSIBILITY_ALWAYS_ON=' not in line:
                line='Exec=/usr/bin/env QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 '+line[5:]
        if main and line=='DBusActivatable=true':line='DBusActivatable=false'
        lines.append(line)
    if not found:raise RuntimeError('No main Dolphin Exec entry found')
    saved={'target':str(target),'existed':target.exists()}
    if target.exists():
        saved.update(original_sha256=digest(target),original_mode=target.stat().st_mode & 0o777)
        atomic(backup,target.read_bytes())
    payload=('\n'.join(lines)+'\n').encode()
    saved['installed_sha256']=hashlib.sha256(payload).hexdigest()
    # Record recovery information first; a failed target write never loses the backup.
    atomic(manifest,json.dumps(saved,indent=2).encode())
    atomic(target,payload)
    print('Configured only the user Dolphin launcher. Shortcuts/services were not changed.')
    print('Run kbuildsycoca6 --noincremental; then close and reopen Dolphin yourself.')

def restore():
    target,manifest,backup=paths();saved=validate(target,manifest,backup)
    if saved is None:
        print('No packaged Dolphin setup to restore.');return
    if saved['existed']:atomic(target,backup.read_bytes(),saved['original_mode'])
    else:target.unlink()
    backup.unlink(missing_ok=True);manifest.unlink()
    try:manifest.parent.rmdir()
    except OSError:pass
    print('Original Dolphin launcher restored; preferences/shortcuts retained.')
    print('Run kbuildsycoca6 --noincremental and reopen Dolphin yourself.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--check',action='store_true',help='read-only legacy conflict check (default)')
    group.add_argument('--enable-dolphin',action='store_true',help='explicitly back up/enable the user accessibility launcher')
    group.add_argument('--restore-dolphin',action='store_true',help='restore only an unchanged launcher created by this helper')
    group.add_argument('--archive-legacy-menu',action='store_true',help='back up only the recognized retired log_selection.py menu')
    args=parser.parse_args()
    if os.geteuid()==0:raise RuntimeError('Run as the desktop user, not root')
    if args.archive_legacy_menu:
        _,data,_,state=locations()
        backup=archive_experimental_menu(data,state)
        print('Retired menu archived: '+str(backup) if backup else 'No retired menu to archive; nothing changed.')
        print('Keyboard shortcuts, preferences and existing backups were not changed.')
    elif args.restore_dolphin:restore()
    elif args.enable_dolphin:enable()
    else:
        check_manual()
        check_command_path()
        print('No legacy conflicts. Run kdeglance-setup --enable-dolphin for Dolphin selection/order support.')
        print('Assign Space manually in Dolphin keyboard shortcuts. Nothing changed.')
    return 0

if __name__=='__main__':
    try:sys.exit(main())
    except (RuntimeError,OSError,ValueError,KeyError) as error:
        print('KDEGlance setup: '+str(error),file=sys.stderr);sys.exit(78)
