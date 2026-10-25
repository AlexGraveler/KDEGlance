#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Start the unchanged viewer only after checking legacy user overrides."""
import os
from pathlib import Path
import sys
from package_support import check_manual

def main():
    if len(sys.argv)<2 or sys.argv[1] not in ('client','daemon'):
        print('Expected client or daemon mode',file=sys.stderr);return 2
    # Metadata-only CLI must work while migration conflicts are being diagnosed.
    if not (sys.argv[1]=='client' and sys.argv[2:]==['--version']):
        try:check_manual()
        except RuntimeError as error:
            print('KDEGlance: '+str(error),file=sys.stderr);return 78
    script=Path(__file__).resolve().with_name(sys.argv[1]+'.py')
    os.execv(sys.executable,[sys.executable,'-B',str(script),*sys.argv[2:]])

if __name__=='__main__':sys.exit(main())
