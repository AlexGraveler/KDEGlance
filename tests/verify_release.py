#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only audit of the public source files; ignore private/build outputs."""
import ast
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent

def public_files():
    for path in sorted(ROOT.rglob('*')):
        relative=path.relative_to(ROOT)
        if any(part in ('.work','.git','__pycache__','.venv','venv') for part in relative.parts):continue
        if path.is_symlink():raise RuntimeError('Unexpected source symlink: '+str(relative))
        if not path.is_file():continue
        if path.suffix in ('.so','.pyc','.pyo','.log') or path.name.endswith(('.tar.gz','.tar.gz.sha256','.so.new')):continue
        yield path

def main():
    files=list(public_files()); names={p.relative_to(ROOT).as_posix() for p in files}
    required={'README.md','INSTALL.md','TESTING.md','CHANGELOG.md','CONTRIBUTING.md','LICENSE',
              'VERSION','.gitignore','THIRD_PARTY.md','RELEASE-NOTES.md','PUBLISH.md',
              'install.sh','uninstall.sh','packaging/install.py','packaging/uninstall.py',
              'kdeglance/native.cpp','kdeglance/COPYING','packaging/distribution.py','packaging/user-README.md'}
    assert required<=names,sorted(required-names)
    sys.path.insert(0,str(ROOT/'kdeglance'))
    from core import VERSION
    assert VERSION=='1.0.0'==(ROOT/'VERSION').read_text().strip()
    assert (ROOT/'LICENSE').read_bytes()==(ROOT/'kdeglance/COPYING').read_bytes()
    assert 'GNU GENERAL PUBLIC LICENSE' in (ROOT/'LICENSE').read_text()
    python_count=0
    for path in files:
        if path.suffix=='.py':ast.parse(path.read_text(),filename=str(path));python_count+=1
        if path.suffix in ('.py','.cpp','.sh','.md','.json','.txt'):
            data=path.read_text()
            # Reject actual developer home paths/credential material, not abstract user examples.
            assert not re.search(r'/h[o]me/[^/\s]+/',data),path.relative_to(ROOT)
            assert not re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',data),path.relative_to(ROOT)
            assert not re.search(r'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})',data),path.relative_to(ROOT)
        if path.suffix=='.md':
            for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)',path.read_text()):
                if '://' in target or target.startswith('#'):continue
                target=target.split('#',1)[0]
                assert (path.parent/target).exists(),(path.relative_to(ROOT),target)
    for script in ('install.sh','uninstall.sh'):
        subprocess.run(['bash','-n',str(ROOT/script)],check=True)
    for name in ('image-preview','image-controls','audio-cover','audio-placeholder','video-preview'):
        assert (ROOT/'screenshots'/(name+'.png')).read_bytes().startswith(bytes.fromhex('89504e470d0a1a0a'))
    print('PASS: public source audit; '+str(len(files))+' files; '+str(python_count)+' Python syntax checks; shell/version/license/docs/screenshots checks.')

if __name__=='__main__':main()
