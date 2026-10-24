#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent
(ROOT/'.work').mkdir(exist_ok=True)
if not (ROOT/'kdeglance/libkdeglance_native.so').exists():
    subprocess.run([sys.executable,'-B',str(ROOT/'kdeglance/build_native.py')],check=True)
sys.exit(subprocess.call(['dbus-run-session','--',sys.executable,'-B',str(ROOT/'tests/test_packaging.py'),*sys.argv[1:]],env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}))
