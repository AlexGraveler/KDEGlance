#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests on an extracted pacman package; never install it in the real root."""
import ctypes
import hashlib
import json
import os
import shlex
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parent.parent
BUILD=ROOT/'.work/aur/build'

class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.suite=tempfile.TemporaryDirectory(prefix='aur-tests-',dir=ROOT/'.work/aur')
        cls.base=Path(cls.suite.name);cls.stage=cls.base/'root';cls.stage.mkdir()
        subprocess.run(['bsdtar','-xf',str(BUILD/'kdeglance-1.0.1-1-x86_64.pkg.tar.zst'),'-C',str(cls.stage)],check=True)
        cls.lib=cls.stage/'usr/lib/kdeglance'
        # Test-only relocation of the shell entry point into the extracted prefix.
        cls.cli=cls.stage/'usr/bin/kdeglance'
        cls.cli.write_text(cls.cli.read_text().replace('/usr/lib/kdeglance/package_launch.py',shlex.quote(str(cls.lib/'package_launch.py'))))
        cls.manual=cls.base/'manual';cls.manual.mkdir()
        subprocess.run(['bsdtar','-xf',str(ROOT/'KDEGlance-1.0.1.tar.gz'),'-C',str(cls.manual)],check=True)
        cls.manual=cls.manual/'KDEGlance-1.0.1/kdeglance'
        shutil.copy2(cls.lib/'libkdeglance_native.so',cls.manual/'libkdeglance_native.so')
    @classmethod
    def tearDownClass(cls):cls.suite.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='user-',dir=self.base)
        self.root=Path(self.temp.name);(self.root/'.kdeglance-test-root').touch()
        self.home=self.root/'home with spaces — тест';self.home.mkdir()
        self.env={**os.environ,'HOME':str(self.home),'XDG_DATA_HOME':str(self.home/'data'),
                  'XDG_CONFIG_HOME':str(self.home/'config'),'XDG_STATE_HOME':str(self.home/'state'),
                  'XDG_CACHE_HOME':str(self.home/'cache'),'XDG_RUNTIME_DIR':str(self.root),
                  'KDEGLANCE_TEST_ROOT':str(self.root),'QT_QPA_PLATFORM':'offscreen','PYTHONDONTWRITEBYTECODE':'1'}
        self.launcher=self.home/'data/applications/org.kde.dolphin.desktop'
    def tearDown(self):self.temp.cleanup()
    def run_py(self,name,*args):
        return subprocess.run([sys.executable,'-B',str(self.lib/name),*args],env=self.env,capture_output=True,text=True,timeout=20)
    def put(self,path,text):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
    def setup(self,*args):return self.run_py('user_setup.py',*args)
    def cli_call(self,*args,env=None):return subprocess.run([str(self.cli),*args],env=env or self.env,text=True,capture_output=True,timeout=20)
    def test_01_layout_versions_source_and_native_library(self):
        files=[p.relative_to(self.stage).as_posix() for p in self.stage.rglob('*') if p.is_file()]
        self.assertTrue(all(name.startswith('usr/') or name.startswith('.') for name in files))
        self.assertFalse(any('/vendor/' in name or '/tests/' in name or '/examples/' in name or name.endswith('.pyc') for name in files))
        info=(self.stage/'.PKGINFO').read_text()
        self.assertIn('pkgname = kdeglance',info);self.assertIn('pkgver = 1.0.1-1',info)
        self.assertEqual(self.run_py('package_launch.py','client','--version').stdout.strip(),'KDEGlance 1.0.1')
        self.assertTrue(hasattr(ctypes.CDLL(str(self.lib/'libkdeglance_native.so')),'kdeglance_activate'))
        for path in self.lib.glob('*.py'):
            original=ROOT/'kdeglance'/path.name
            if original.exists():self.assertEqual(path.read_bytes(),original.read_bytes(),path.name)
        self.assertEqual((self.stage/'usr/bin/kdeglance2').readlink(),Path('kdeglance'))
    def test_02_check_is_read_only_and_preferences_do_not_conflict(self):
        settings=self.home/'config/kdeglance2/audio.ini';self.put(settings,'[General]\nvolume=0.37\n')
        before={str(p):p.read_bytes() for p in self.home.rglob('*') if p.is_file()}
        self.assertEqual(self.setup('--check').returncode,0)
        self.assertEqual(before,{str(p):p.read_bytes() for p in self.home.rglob('*') if p.is_file()})
    def test_03_manual_override_is_refused_before_launch(self):
        for name in ('config/systemd/user/kdeglance2.service','data/dbus-1/services/io.github.KDEGlance2.service',
                     'data/kio/servicemenus/kdeglance-space-probe.desktop','.local/bin/kdeglance'):
            with self.subTest(name=name):
                path=self.home/name;self.put(path,'do not change\n')
                for args in [('package_launch.py','client','state'),('package_launch.py','daemon'),('user_setup.py','--check')]:
                    result=self.run_py(*args);self.assertEqual(result.returncode,78,result.stderr)
                    self.assertIn('Manual KDEGlance',result.stderr)
                self.assertEqual(path.read_text(),'do not change\n');path.unlink()
    def test_04_setup_restore_and_idempotence(self):
        original='[Desktop Entry]\nType=Application\nName=Custom Dolphin\nExec=dolphin --new-window %U\nDBusActivatable=true\n'
        self.put(self.launcher,original)
        shortcuts=self.home/'config/kglobalshortcutsrc';self.put(shortcuts,'keep=Space\n')
        self.assertEqual(self.setup('--enable-dolphin').returncode,0)
        installed=self.launcher.read_text()
        self.assertIn('QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1',installed)
        self.assertIn('DBusActivatable=false',installed)
        self.assertEqual(self.setup('--enable-dolphin').returncode,0)
        self.assertEqual(self.launcher.read_text(),installed)
        self.assertEqual(self.setup('--restore-dolphin').returncode,0)
        self.assertEqual(self.launcher.read_text(),original)
        self.assertEqual(self.setup('--restore-dolphin').returncode,0)
        self.assertEqual(shortcuts.read_text(),'keep=Space\n')
    def test_05_modified_launcher_and_backup_are_preserved(self):
        self.put(self.launcher,'[Desktop Entry]\nName=Dolphin\nExec=dolphin %U\n')
        self.assertEqual(self.setup('--enable-dolphin').returncode,0)
        installed=self.launcher.read_bytes();self.launcher.write_bytes(installed+b'# user edit\n')
        self.assertEqual(self.setup('--restore-dolphin').returncode,78)
        self.assertTrue(self.launcher.read_bytes().endswith(b'# user edit\n'))
        self.launcher.write_bytes(installed)
        backup=self.home/'state/kdeglance-aur/dolphin-original.desktop';backup.write_text('modified backup')
        self.assertEqual(self.setup('--restore-dolphin').returncode,78)
        self.assertEqual(self.launcher.read_bytes(),installed)
    def test_06_symlinks_are_refused(self):
        victim=self.home/'keep';victim.write_text('keep')
        self.launcher.parent.mkdir(parents=True);self.launcher.symlink_to(victim)
        self.assertEqual(self.setup('--enable-dolphin').returncode,78)
        self.assertTrue(self.launcher.is_symlink());self.assertEqual(victim.read_text(),'keep')
    def test_07_manual_migration_preserves_preferences(self):
        settings=self.home/'config/kdeglance2/audio.ini';self.put(settings,'[General]\nvolume=0.37\n')
        def manage(command):return subprocess.run([sys.executable,'-B',str(self.manual/'manage.py'),command,'--isolated'],env=self.env,text=True,capture_output=True,timeout=20)
        result=manage('install');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(self.setup('--check').returncode,78)
        self.assertEqual(self.run_py('package_launch.py','client','state').returncode,78)
        result=manage('uninstall');self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(self.setup('--check').returncode,0)
        self.assertIn('0.37',settings.read_text())
        self.assertEqual(self.run_py('package_launch.py','client','--version').returncode,0)
    def test_08_optional_new_launcher_restore_does_not_remove_other_files(self):
        unrelated=self.home/'state/kdeglance-aur/keep';self.put(unrelated,'keep')
        self.assertEqual(self.setup('--enable-dolphin').returncode,0)
        self.assertTrue(self.launcher.exists())
        self.assertEqual(self.setup('--restore-dolphin').returncode,0)
        self.assertFalse(self.launcher.exists());self.assertEqual(unrelated.read_text(),'keep')
    def test_09_disabled_custom_accessibility_setting_is_preserved(self):
        original='[Desktop Entry]\nExec=/usr/bin/env QT_LINUX_ACCESSIBILITY_ALWAYS_ON=0 dolphin %U\n'
        self.put(self.launcher,original)
        self.assertEqual(self.setup('--enable-dolphin').returncode,78)
        self.assertEqual(self.launcher.read_text(),original)

    def test_10_packaged_media_and_single_daemon_on_private_bus(self):
        for name in ('audio','video'):self.put(self.home/('config/kdeglance2/'+name+'.ini'),'[General]\nvolume=0\n')
        log=(self.root/'daemon.log').open('w')
        daemon=subprocess.Popen([sys.executable,'-B',str(self.lib/'package_launch.py'),'daemon'],env=self.env,stdout=log,stderr=log)
        def state():
            result=self.cli_call('state')
            if result.returncode:raise RuntimeError(result.stderr)
            return json.loads(result.stdout)
        def wait(condition):
            deadline=time.monotonic()+12
            while time.monotonic()<deadline:
                try:
                    value=state()
                    if condition(value):return value
                except (RuntimeError,ValueError):pass
                time.sleep(.05)
            raise RuntimeError('Packaged media state timeout')
        try:
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                owner=subprocess.run(['busctl','--user','call','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','NameHasOwner','s','io.github.KDEGlance2'],env=self.env,capture_output=True,text=True)
                if owner.stdout.strip()=='b true':break
                self.assertIsNone(daemon.poll());time.sleep(.05)
            self.assertEqual(wait(lambda s:s['version']=='1.0.1')['pid'],daemon.pid)
            duplicate=self.run_py('package_launch.py','daemon');self.assertEqual(duplicate.returncode,0)
            self.assertEqual(state()['pid'],daemon.pid)
            examples=ROOT/'kdeglance/examples'
            for path,module in [(next(examples.glob('*.png')),'image'),(examples/'audio/preview.mp3','audio'),(examples/'video/preview.mp4','video')]:
                result=self.cli_call('open','--',str(path));self.assertEqual(result.returncode,0,result.stderr)
                value=wait(lambda s:s['module']==module and s['visible'] and (s.get('phase')=='ready' if module=='image' else (s.get('audio',{}).get('playing',False) if module=='audio' else s.get('video',{}).get('frames_received',0)>2)))
                self.assertEqual(value['pid'],daemon.pid)
            self.assertEqual(self.cli_call('close').returncode,0)
        finally:
            daemon.terminate();daemon.wait(timeout=10);log.close()

    def test_11_cli_shadowing_version_and_unavailable_bus(self):
        oldbin=self.root/'oldbin';oldbin.mkdir()
        old=oldbin/'kdeglance';old.write_text('#!/bin/sh\necho "KDEGlance: no valid image path was supplied." >&2\nexit 1\n');old.chmod(0o755)
        env={**self.env,'PATH':str(oldbin)+':'+str(self.cli.parent)+':'+self.env['PATH']}
        broken=subprocess.run(['kdeglance','--version'],env=env,text=True,capture_output=True)
        self.assertIn('no valid image path',broken.stderr)
        check=self.run_with_env('user_setup.py','--check',env=env)
        self.assertEqual(check.returncode,78);self.assertIn('shadowed by:',check.stderr)
        self.assertEqual(self.cli_call('--version',env=env).stdout.strip(),'KDEGlance 1.0.1')
        old.rename(oldbin/'kdeglance.saved')
        fixed=subprocess.run(['kdeglance','--version'],env=env,text=True,capture_output=True)
        self.assertEqual(fixed.returncode,0);self.assertEqual(fixed.stdout.strip(),'KDEGlance 1.0.1')
        self.assertEqual(self.run_with_env('user_setup.py','--check',env=env).returncode,0)
        env['DBUS_SESSION_BUS_ADDRESS']='unix:path='+str(self.root/'missing-bus')
        result=self.cli_call('state',env=env)
        self.assertNotEqual(result.returncode,0);self.assertIn('KDEGlance:',result.stderr)
        self.assertNotIn('image path',result.stderr);self.assertNotIn('Traceback',result.stderr)

    def run_with_env(self,name,*args,env):
        return subprocess.run([sys.executable,'-B',str(self.lib/name),*args],env=env,text=True,capture_output=True,timeout=20)

    def test_12_version_works_during_migration(self):
        self.put(self.home/'config/systemd/user/kdeglance2.service','old installation')
        result=self.cli_call('--version')
        self.assertEqual(result.returncode,0);self.assertEqual(result.stdout.strip(),'KDEGlance 1.0.1')
        result=self.cli_call('state')
        self.assertEqual(result.returncode,78);self.assertIn('Manual KDEGlance',result.stderr)

    def test_13_packaged_probe_archiving_is_explicit_and_preserves_shortcuts(self):
        data=self.home/'data';menu=data/'kio/servicemenus/kdeglance-space-probe.desktop'
        payload=('[Desktop Entry]\nType=Service\nActions=LogSelection;\n[Desktop Action LogSelection]\nName=KDEGlance probe\nExec=/usr/bin/python3 '+shlex.quote(str(data/'kdeglance-space-probe/log_selection.py'))+' %U\n')
        self.put(menu,payload)
        shortcuts=self.home/'config/kglobalshortcutsrc';self.put(shortcuts,'Space=keep\n')
        self.assertEqual(self.setup('--check').returncode,78)
        self.assertEqual(menu.read_text(),payload)
        result=self.setup('--archive-legacy-menu');self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(menu.exists())
        backup=next((self.home/'state/kdeglance-migration').glob('*.desktop'))
        self.assertEqual(backup.read_text(),payload);self.assertEqual(shortcuts.read_text(),'Space=keep\n')
        self.assertEqual(self.setup('--check').returncode,0)
        self.assertEqual(self.setup('--archive-legacy-menu').returncode,0)

if __name__=='__main__':unittest.main(verbosity=2)
