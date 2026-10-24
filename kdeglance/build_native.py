#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the activation bridge against the target system's Qt/KF headers."""
from pathlib import Path
import os
import shlex
import subprocess
root = Path(__file__).resolve().parent
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Gui >= 6.8", "KF6WindowSystem >= 6.0"], text=True))
target = root/"libkdeglance_native.so"
temporary = root/"libkdeglance_native.so.new"
try:
    subprocess.run(["g++", "-std=c++20", "-fPIC", "-shared", str(root/"native.cpp"), "-o", str(temporary), *flags], check=True)
    os.replace(temporary,target)
finally:
    temporary.unlink(missing_ok=True)
print(target.name)
