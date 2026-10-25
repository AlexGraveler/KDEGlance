#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Retired experiment recognition/backups; all writes stay in disposable HOME."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'kdeglance'))
import legacy

class LegacyTests(unittest.TestCase):
    def setUp(self):
        work=ROOT/'.work';work.mkdir(exist_ok=True)
        self.temp=tempfile.TemporaryDirectory(prefix='legacy-',dir=work)
        self.root=Path(self.temp.name);(self.root/'.kdeglance-test-root').touch()
        self.home=self.root/'home with spaces — тест';self.home.mkdir()
        self.data=self.home/'data';self.state=self.home/'state';self.config=self.home/'config'
        self.menu=self.data/'kio/servicemenus'/legacy.MENU;self.menu.parent.mkdir(parents=True)
        self.env={**os.environ,'HOME':str(self.home),'XDG_DATA_HOME':str(self.data),'XDG_STATE_HOME':str(self.state),
                  'XDG_CONFIG_HOME':str(self.config),'XDG_CACHE_HOME':str(self.home/'cache'),
                  'XDG_RUNTIME_DIR':str(self.root),'KDEGLANCE_TEST_ROOT':str(self.root),'PYTHONDONTWRITEBYTECODE':'1'}
        self.payload=('[Desktop Entry]\nType=Service\nName=KDEGlance Space Probe\nMimeType=all/allfiles;\nActions=LogSelection;\n\n[Desktop Action LogSelection]\nName=Log Selection\nExec=/usr/bin/python3 -B '+shlex.quote(str(self.data/'kdeglance-space-probe/log_selection.py'))+' %U\n').encode()
    def tearDown(self):self.temp.cleanup()
    def manage(self,*args):return subprocess.run([sys.executable,'-B',str(ROOT/'kdeglance/manage.py'),*args,'--isolated'],env=self.env,text=True,capture_output=True,timeout=20)
    def test_01_recognition_rejects_modified_commands_and_unknown_sections(self):
        self.assertTrue(legacy.is_experimental_menu(self.payload,self.data))
        for payload in [self.payload.replace(b'log_selection.py',b'other.py'),self.payload.replace(b' %U',b' %U ; touch /tmp/unrelated'),self.payload+b'\n[Other Action]\nExec=other\n',self.payload.replace(b'LogSelection;',b'LogSelection;Other;'),self.payload.replace(b'/usr/bin/python3',b'/bin/sh')]:
            with self.subTest(payload=payload):self.assertFalse(legacy.is_experimental_menu(payload,self.data))
    def test_02_archive_is_lossless_and_repeatable(self):
        self.menu.write_bytes(self.payload)
        settings=self.config/'kglobalshortcutsrc';settings.parent.mkdir(parents=True);settings.write_text('Space=keep\n')
        backup=legacy.archive_experimental_menu(self.data,self.state)
        self.assertFalse(self.menu.exists());self.assertEqual(backup.read_bytes(),self.payload)
        record=json.loads(backup.with_suffix('.json').read_text())
        self.assertEqual(record['sha256'],hashlib.sha256(self.payload).hexdigest())
        self.assertEqual(settings.read_text(),'Space=keep\n')
        self.assertIsNone(legacy.archive_experimental_menu(self.data,self.state))
    def test_03_unknown_files_and_symlinks_are_not_moved(self):
        self.menu.write_bytes(b'[Desktop Entry]\nType=Service\nName=My custom action\n')
        with self.assertRaises(RuntimeError):legacy.archive_experimental_menu(self.data,self.state)
        self.assertTrue(self.menu.exists());self.assertFalse(self.state.exists())
        self.menu.unlink();victim=self.root/'keep';victim.write_bytes(self.payload);self.menu.symlink_to(victim)
        with self.assertRaises(RuntimeError):legacy.archive_experimental_menu(self.data,self.state)
        self.assertTrue(self.menu.is_symlink());self.assertEqual(victim.read_bytes(),self.payload)
    def test_04_concurrent_change_is_preserved(self):
        self.menu.write_bytes(self.payload);save=legacy.save_backup
        def change(*args):
            backup=save(*args);self.menu.write_bytes(b'modified by user');return backup
        with patch('legacy.save_backup',side_effect=change):
            with self.assertRaises(RuntimeError):legacy.archive_experimental_menu(self.data,self.state)
        self.assertEqual(self.menu.read_bytes(),b'modified by user')
    def test_05_explicit_uninstall_archives_leftover_without_manifest(self):
        self.menu.write_bytes(self.payload)
        result=self.manage('uninstall');self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(self.menu.exists())
        self.assertEqual(next((self.state/'kdeglance-migration').glob('*.desktop')).read_bytes(),self.payload)
        self.assertEqual(self.manage('uninstall').returncode,0)
    def test_06_original_probe_is_not_reactivated_after_managed_uninstall(self):
        result=self.manage('install');self.assertEqual(result.returncode,0,result.stderr)
        manifest_path=self.state/'kdeglance2/installation.json';manifest=json.loads(manifest_path.read_text())
        backup=Path(manifest['latest_backup'])/'retired-probe';backup.write_bytes(self.payload)
        manifest['original'][str(self.menu)]={'existed':True,'copy':str(backup),'sha256':hashlib.sha256(self.payload).hexdigest(),'mode':0o600}
        manifest_path.write_text(json.dumps(manifest))
        settings=self.config/'kdeglance2/audio.ini';settings.parent.mkdir(parents=True,exist_ok=True);settings.write_text('volume=0.37')
        result=self.manage('uninstall');self.assertEqual(result.returncode,0,result.stderr)
        self.assertFalse(self.menu.exists());self.assertEqual(backup.read_bytes(),self.payload)
        self.assertEqual(next((self.state/'kdeglance-migration').glob('*.desktop')).read_bytes(),self.payload)
        self.assertEqual(settings.read_text(),'volume=0.37')

if __name__=='__main__':unittest.main(verbosity=2)
