#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent

def main():
    parser=argparse.ArgumentParser(description='Remove only KDEGlance-owned user files')
    parser.add_argument('--purge-settings',action='store_true',help='ask before removing audio/video preferences')
    parser.add_argument('--test-isolated',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if os.geteuid()==0:raise RuntimeError('Run as your normal desktop user, not root.')
    sys.path.insert(0,str(ROOT/'kdeglance'))
    from manage import validate_test_environment,CONFIG
    if args.test_isolated:validate_test_environment()
    if args.purge_settings and not args.test_isolated:
        print('This also removes only KDEGlance audio.ini/video.ini preferences. Logs and backups remain.')
        if not sys.stdin.isatty() or input('Remove preferences too? [y/N] ').strip().lower() not in ('y','yes'):
            raise RuntimeError('Preference deletion declined; nothing removed.')
    command=[sys.executable,'-B',str(ROOT/'kdeglance/manage.py'),'uninstall']
    if args.test_isolated:command.append('--isolated')
    subprocess.run(command,check=True)
    if args.purge_settings:
        for name in ('audio.ini','video.ini'):
            path=CONFIG/'kdeglance2'/name
            if path.is_symlink():raise RuntimeError('Refusing symlinked preferences: '+str(path))
            path.unlink(missing_ok=True)
    if not args.test_isolated:
        subprocess.run(['kbuildsycoca6','--noincremental'],check=True)
    print('Done. Shared packages, unrelated applications, and shortcut settings were not removed.')
    return 0

if __name__=='__main__':
    try:sys.exit(main())
    except (RuntimeError,OSError,ValueError,subprocess.SubprocessError) as error:
        print('KDEGlance removal: '+str(error),file=sys.stderr);sys.exit(1)
