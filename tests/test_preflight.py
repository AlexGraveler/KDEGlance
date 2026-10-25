#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real shell installer with missing packages/imports, without sudo."""
import os
from pathlib import Path
import pty
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent.parent

class PreflightTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'.work').mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='preflight-',dir=ROOT/'.work')
        self.root=Path(self.temp.name)
        self.home=self.root/'home with spaces — тест';self.home.mkdir()
        self.bin=self.root/'bin';self.bin.mkdir()
        self.hook=self.root/'hooks';self.hook.mkdir()
        self.state=self.root/'missing-packages';self.state.write_text('')
        self.calls=self.root/'sudo-calls';self.imports=self.root/'unexpected-imports'
        self.env={**os.environ,'HOME':str(self.home),'XDG_DATA_HOME':str(self.home/'data'),
                  'XDG_CONFIG_HOME':str(self.home/'config'),'XDG_STATE_HOME':str(self.home/'state'),
                  'XDG_CACHE_HOME':str(self.home/'cache'),'XDG_RUNTIME_DIR':str(self.root),
                  'XDG_CURRENT_DESKTOP':'KDE','XDG_SESSION_TYPE':'wayland',
                  'PYTHONDONTWRITEBYTECODE':'1','PYTHONPATH':str(self.hook),
                  'PATH':str(self.bin)+':'+os.environ['PATH'],
                  'KDEGLANCE_PREFLIGHT_STATE':str(self.state),'KDEGLANCE_PREFLIGHT_CALLS':str(self.calls),
                  'KDEGLANCE_PREFLIGHT_IMPORTS':str(self.imports),'KDEGLANCE_PREFLIGHT_BLOCK_QT':'missing'}
        self.script('pacman', '''case "$1" in
-T) if [ -s "$KDEGLANCE_PREFLIGHT_STATE" ]; then cat "$KDEGLANCE_PREFLIGHT_STATE"; exit 127; fi ;;
-Q) case "$2" in dolphin) echo 'dolphin 26.08.2-1';; *) echo 'plasma-workspace 6.8.0-1';; esac ;;
*) exit 91;;
esac
''')
        self.script('systemctl','exit 0\n')
        self.script('pkg-config','exit 0\n')
        self.script('sudo','''printf '%s\\n' "$@" > "$KDEGLANCE_PREFLIGHT_CALLS"
: > "$KDEGLANCE_PREFLIGHT_STATE"
''')
        (self.hook/'sitecustomize.py').write_text('''import os,sys
from pathlib import Path
class MissingDependency:
    def find_spec(self,fullname,path=None,target=None):
        app=fullname.split('.')[0] in {'core','manage','audio','video','daemon','window','loader','remote','visuals','interactions','dolphin_order'}
        qt=fullname.split('.')[0] in {'PyQt6','gi'}
        missing=bool(Path(os.environ['KDEGLANCE_PREFLIGHT_STATE']).read_text().strip())
        blocked=os.environ.get('KDEGLANCE_PREFLIGHT_BLOCK_QT')=='always' or missing
        if app or (qt and blocked):
            with Path(os.environ['KDEGLANCE_PREFLIGHT_IMPORTS']).open('a') as stream:stream.write(fullname+'\\n')
            raise ModuleNotFoundError('Simulated unavailable dependency: '+fullname,name=fullname)
sys.meta_path.insert(0,MissingDependency())
''')
    def tearDown(self):self.temp.cleanup()
    def script(self,name,body):
        path=self.bin/name;path.write_text('#!/bin/sh\n'+body);path.chmod(0o755)
    def configure(self,missing):
        self.state.write_text(''.join(name+'\n' for name in missing))
        self.calls.unlink(missing_ok=True);self.imports.unlink(missing_ok=True)
    def run_check(self,*args):
        return subprocess.run(['bash',str(ROOT/'install.sh'),*args],env=self.env,cwd=self.root,
                              text=True,capture_output=True,timeout=20)
    def unchanged(self,result):
        self.assertNotIn('Traceback',result.stdout+result.stderr)
        self.assertEqual(list(self.home.rglob('*')),[],'Preflight wrote to HOME/XDG')
    def test_01_check_missing_one_both_and_none(self):
        for missing in [('python-pyqt6',),('qt6-wayland',),('python-pyqt6','qt6-wayland'),()]:
            with self.subTest(missing=missing):
                self.configure(missing)
                result=self.run_check('--check');self.unchanged(result)
                self.assertEqual(result.returncode,1 if missing else 0,result.stdout+result.stderr)
                if missing:self.assertIn('Missing packages: '+' '.join(missing),result.stdout)
                else:self.assertIn('Check complete.',result.stdout)
                self.assertFalse(self.imports.exists(),'Imported application/absent runtime before preflight')
                self.assertFalse(self.calls.exists(),'--check ran sudo')
    def test_02_normal_install_proposes_exact_missing_packages_and_refuses_noninteractive(self):
        for missing in [('python-pyqt6',),('qt6-wayland',),('python-pyqt6','qt6-wayland')]:
            with self.subTest(missing=missing):
                self.configure(missing);result=self.run_check();self.unchanged(result)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('sudo pacman -Syu --needed '+' '.join(missing),result.stdout)
                self.assertIn('Dependency installation declined',result.stderr)
                self.assertFalse(self.calls.exists());self.assertFalse(self.imports.exists())
    def test_03_confirm_package_install_then_cancel_application_install(self):
        self.configure(('python-pyqt6','qt6-wayland'))
        master,slave=pty.openpty()
        try:
            process=subprocess.Popen(['bash',str(ROOT/'install.sh')],env=self.env,cwd=self.root,
                                     stdin=slave,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            os.close(slave);slave=None
            os.write(master,b'y\nn\n')
            output,error=process.communicate(timeout=20)
        finally:
            os.close(master)
            if slave is not None:os.close(slave)
            if 'process' in locals() and process.poll() is None:
                process.kill();process.communicate()
        result=subprocess.CompletedProcess(process.args,process.returncode,output,error)
        self.unchanged(result)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.calls.read_text().splitlines(),['pacman','-Syu','--needed','python-pyqt6','qt6-wayland'])
        self.assertEqual(self.state.read_text(),'')
        self.assertFalse(self.imports.exists())
        self.assertIn('Installation cancelled',error)
    def test_04_broken_runtime_import_is_reported_without_traceback(self):
        self.configure(())
        self.env['KDEGLANCE_PREFLIGHT_BLOCK_QT']='always'
        result=self.run_check('--check');self.unchanged(result)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Python/Qt/Gio check:',result.stdout)
        self.assertFalse(self.calls.exists())

if __name__=='__main__':unittest.main(verbosity=2)
