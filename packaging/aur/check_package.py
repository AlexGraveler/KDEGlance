# SPDX-License-Identifier: GPL-3.0-or-later
"""Build-time checks use only stdlib, never the desktop or user service manager."""
import ast
import configparser
from pathlib import Path
import sys
root=Path(sys.argv[1]);source=root/'KDEGlance-1.0.1'
assert (source/'VERSION').read_text().strip()=='1.0.1'
for folder in (source/'kdeglance',root):
    for path in folder.glob('*.py'):ast.parse(path.read_text(),filename=str(path))
for name,section in [('io.github.KDEGlance2.desktop','Desktop Entry'),('kdeglance-space-probe.desktop','Desktop Entry'),('io.github.KDEGlance2.service','D-BUS Service'),('kdeglance2.service','Service')]:
    parser=configparser.ConfigParser(interpolation=None,strict=False);parser.optionxform=str;parser.read(root/name)
    assert section in parser
    assert not any('/home/' in value for values in parser.values() for value in values.values())
    if name=='kdeglance-space-probe.desktop':
        assert parser[section]['Actions']=='LogSelection;'
        assert parser['Desktop Action LogSelection']['Exec']=='/usr/bin/kdeglance toggle -- %U'
print('PASS: package source syntax, version, fixed paths and integration templates (no GUI/session changes).')
