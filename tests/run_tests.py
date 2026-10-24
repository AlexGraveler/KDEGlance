#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build locally; run viewer or GIF tests on a private D-Bus session."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parent.parent
work=ROOT/'.work';work.mkdir(exist_ok=True)
if not (ROOT/'kdeglance/libkdeglance_native.so').is_file():
    subprocess.run([sys.executable,'-B',str(ROOT/'kdeglance/build_native.py')],check=True)
arguments=sys.argv[1:]
filename='test_viewer.py'
if arguments and arguments[0]=='--gif':filename='test_gif.py';arguments=arguments[1:]
with tempfile.TemporaryDirectory(prefix='session-',dir=work) as directory:
    root=Path(directory);(root/'.kdeglance-test-root').touch()
    env={**os.environ,'HOME':directory,'XDG_RUNTIME_DIR':directory,'KDEGLANCE_TEST_ROOT':directory,
         'XDG_CONFIG_HOME':str(root/'config'),'XDG_DATA_HOME':str(root/'data'),
         'XDG_CACHE_HOME':str(root/'cache'),'XDG_STATE_HOME':str(root/'state'),
         'QT_QPA_PLATFORM':'offscreen','PYTHONDONTWRITEBYTECODE':'1'}
    sys.exit(subprocess.call(['dbus-run-session','--',sys.executable,'-B',
                             str(ROOT/'tests'/filename),*arguments],env=env))
