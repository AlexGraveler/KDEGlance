#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Use the common deterministic source export for both distributions."""
from pathlib import Path
import shutil
import subprocess
import re
root = Path(__file__).resolve().parents[2]
version = re.search(r"project\(kdeglance VERSION ([0-9.]+)", (root/"CMakeLists.txt").read_text()).group(1)
subprocess.run(["bash", str(root/"packaging/arch/make-source-tarball.sh")], check=True)
source = root/"packaging/arch"/("kdeglance-"+version+".tar.gz")
output = root/"packaging/fedora/rpmbuild/SOURCES"/source.name
output.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source, output)
print(output)
