#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os
from pathlib import Path
import subprocess
import sys
from check_env import ROOT,probe,show

def confirm(message):
    return sys.stdin.isatty() and input(message+' [y/N] ').strip().lower() in ('y','yes')

def main():
    parser=argparse.ArgumentParser(description='User-level KDEGlance 1.0.1 installer')
    parser.add_argument('--check',action='store_true',help='read-only requirements check')
    parser.add_argument('--test-isolated',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if os.geteuid()==0:raise RuntimeError('Run as your normal desktop user, not root.')
    source=ROOT/'kdeglance'
    sys.path.insert(0,str(source))
    # Bootstrap must use only the standard library: core imports PyQt6.
    VERSION=(ROOT/'VERSION').read_text().strip()
    if VERSION!='1.0.1':raise RuntimeError('Release version mismatch.')
    if args.test_isolated:
        from manage import validate_test_environment
        validate_test_environment()
    else:
        result=probe();show(result)
        if not result['supported']:return 1
        if result['missing'] and not args.check:
            command=['sudo','pacman','-Syu','--needed',*result['missing']]
            print('This updates the system and installs missing packages: '+' '.join(command))
            if not confirm('Run this package-manager command?'):raise RuntimeError('Dependency installation declined; KDEGlance was not installed.')
            subprocess.run(command,check=True)
            result=probe();show(result)
        if not result['supported']:return 1
        if result['missing'] or result['errors']:return 1
    if args.check:
        print('Check complete. No files, shortcuts or services changed.');return 0
    print('Install under your XDG data/config directories; add kdeglance and compatibility commands;')
    print('create/refresh KDEGlance D-Bus activation, the user service and Dolphin action;')
    print('add an accessibility-enabled user Dolphin launcher and restart kdeglance2.service.')
    print('Existing files are backed up. Space is configured manually; no shortcut settings are edited.')
    if not args.test_isolated and not confirm('Install/update KDEGlance?'):raise RuntimeError('Installation cancelled.')
    subprocess.run([sys.executable,'-B',str(source/'build_native.py')],check=True)
    command=[sys.executable,'-B',str(source/'manage.py'),'install']
    if args.test_isolated:command.append('--isolated')
    subprocess.run(command,check=True)
    if not args.test_isolated:
        subprocess.run(['kbuildsycoca6','--noincremental'],check=True)
        result=subprocess.run([str(Path.home()/'.local/bin/kdeglance'),'state'],check=True,capture_output=True,text=True)
        import json
        state=json.loads(result.stdout)
        if state.get('version')!=VERSION:raise RuntimeError('The running daemon version does not match the release.')
        print('Verified running version: '+VERSION)
    print('Installed. Reopen Dolphin; see README.md to assign Space without overwriting another shortcut.')
    return 0

if __name__=='__main__':
    try:sys.exit(main())
    except (RuntimeError,OSError,ValueError,ImportError,subprocess.SubprocessError) as error:
        print('KDEGlance installation: '+str(error),file=sys.stderr);sys.exit(1)
