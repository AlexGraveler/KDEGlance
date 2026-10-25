# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only detection of per-user overrides of the packaged KDEGlance identity."""
import os
import shutil
from pathlib import Path

SYSTEM_UNIT=Path('/usr/lib/systemd/user/kdeglance2.service')

def locations():
    home=Path.home()
    return home,Path(os.environ.get('XDG_DATA_HOME') or home/'.local/share'),Path(os.environ.get('XDG_CONFIG_HOME') or home/'.config'),Path(os.environ.get('XDG_STATE_HOME') or home/'.local/state')

def manual_conflicts():
    home,data,config,state=locations()
    candidates=[(data/'kdeglance2/current',()),(data/'kdeglance2/.owned-by-kdeglance2',()),
                (state/'kdeglance2/installation.json',()),
                (config/'systemd/user/kdeglance2.service',(SYSTEM_UNIT,)),
                (data/'dbus-1/services/io.github.KDEGlance2.service',()),
                (data/'kio/servicemenus/kdeglance-space-probe.desktop',()),
                (data/'applications/io.github.KDEGlance2.desktop',()),
                (home/'.local/bin/kdeglance',(Path('/usr/bin/kdeglance'),)),
                (home/'.local/bin/kdeglance2',(Path('/usr/bin/kdeglance2'),Path('/usr/bin/kdeglance'))),
                (home/'.local/bin/kdeglance-manage',())]
    conflicts=[]
    for path,allowed in candidates:
        if path.is_symlink() and path.resolve() in allowed:continue
        if path.exists() or path.is_symlink():conflicts.append(path)
    return conflicts

def check_manual():
    conflicts=manual_conflicts()
    if conflicts:
        message='Manual KDEGlance installation/override detected; package launch refused.\n'
        message+='\n'.join('  '+str(path) for path in conflicts)
        message+='\nUse the manual installer uninstall first; preserve preferences and backups.\n'
        message+='For the retired log_selection.py Service Menu only, use kdeglance-setup --archive-legacy-menu.\n'
        message+='See /usr/share/doc/kdeglance/AUR-INSTALL.md. No user files were changed.'
        raise RuntimeError(message)


def check_command_path():
    selected=shutil.which('kdeglance')
    expected=Path(__file__).resolve().parents[2]/'bin/kdeglance'
    if selected and Path(selected).resolve()!=expected.resolve():
        raise RuntimeError("Command kdeglance is shadowed by: "+selected+"\nUse /usr/bin/kdeglance --version or /usr/bin/kdeglance state.\nReview the legacy launcher; do not overwrite it blindly. See the migration guide.")
