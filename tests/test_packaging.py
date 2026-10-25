#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Disposable user installation tests. No real service or desktop is touched."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'kdeglance'))
sys.path.insert(0,str(ROOT/'packaging'))
import check_env

class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='package-',dir=ROOT/'.work')
        self.root=Path(self.temp.name);(self.root/'.kdeglance-test-root').touch()
        self.home=self.root/'home with spaces — тест';self.home.mkdir()
        self.env={**os.environ,'HOME':str(self.home),'XDG_CONFIG_HOME':str(self.home/'config'),
                  'XDG_DATA_HOME':str(self.home/'data'),'XDG_STATE_HOME':str(self.home/'state'),
                  'XDG_CACHE_HOME':str(self.home/'cache'),'XDG_RUNTIME_DIR':str(self.root),
                  'KDEGLANCE_TEST_ROOT':str(self.root),'QT_QPA_PLATFORM':'offscreen','PYTHONDONTWRITEBYTECODE':'1'}
        self.current=self.home/'data/kdeglance2/current'
        self.manifest=self.home/'state/kdeglance2/installation.json'
        self.foreign=self.root/'outside cwd';self.foreign.mkdir()
    def tearDown(self):self.temp.cleanup()
    def run_command(self,*args,env=None):
        return subprocess.run(args,cwd=self.foreign,env=env or self.env,text=True,capture_output=True,timeout=30)
    def install(self):
        result=self.run_command('bash',str(ROOT/'install.sh'),'--test-isolated')
        self.assertEqual(result.returncode,0,result.stderr)
    def uninstall(self,*options):
        return self.run_command('bash',str(ROOT/'uninstall.sh'),'--test-isolated',*options)
    def test_01_version_runtime_license_and_scripts(self):
        from core import VERSION
        from manage import RUNTIME
        self.assertEqual(VERSION,'1.0.1');self.assertEqual((ROOT/'VERSION').read_text().strip(),VERSION)
        self.assertIn('GNU GENERAL PUBLIC LICENSE',(ROOT/'LICENSE').read_text())
        self.assertFalse(set(check_env.PACKAGES) & {'desktop-file-utils','python-pillow','ffmpeg','libheif','pytest','git'})
        for name in RUNTIME:self.assertTrue((ROOT/'kdeglance'/name).exists(),name)
        for name in ('install.sh','uninstall.sh'):
            self.assertEqual(self.run_command('bash','-n',str(ROOT/name)).returncode,0)
    def test_02_repeated_install_remove_and_custom_launcher_restore(self):
        launcher=self.home/'data/applications/org.kde.dolphin.desktop'
        launcher.parent.mkdir(parents=True)
        original='[Desktop Entry]\nType=Application\nName=Custom Dolphin\nExec=dolphin --new-window %u\n'
        launcher.write_text(original)
        preferences=self.home/'config/kdeglance2/audio.ini';preferences.parent.mkdir(parents=True);preferences.write_text('[General]\nvolume=0.42\n')
        self.install();self.install()
        state=json.loads(self.manifest.read_text());self.assertEqual(state['version'],'1.0.1')
        result=self.run_command(str(self.home/'.local/bin/kdeglance'),'--version')
        self.assertEqual(result.returncode,0,result.stderr);self.assertEqual(result.stdout.strip(),'KDEGlance 1.0.1')
        menu=(self.home/'data/kio/servicemenus/kdeglance-space-probe.desktop').read_text()
        self.assertIn('Actions=LogSelection;',menu);self.assertIn(' toggle -- %U',menu)
        self.assertEqual(launcher.read_text().count('QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1'),1)
        self.assertEqual(self.uninstall().returncode,0)
        self.assertEqual(launcher.read_text(),original);self.assertTrue(preferences.exists());self.assertFalse(self.current.exists())
        self.assertEqual(self.uninstall().returncode,0)
    def test_03_manifest_cannot_remove_foreign_file(self):
        self.install();victim=self.home/'do-not-delete';victim.write_text('keep')
        data=json.loads(self.manifest.read_text());data['installed'][str(victim)]=hashlib.sha256(victim.read_bytes()).hexdigest()
        data['original'][str(victim)]={'existed':False,'copy':None};self.manifest.write_text(json.dumps(data))
        self.assertNotEqual(self.uninstall().returncode,0);self.assertEqual(victim.read_text(),'keep');self.assertTrue(self.current.exists())
    def test_04_changed_integration_is_not_deleted(self):
        self.install();path=self.home/'data/applications/io.github.KDEGlance2.desktop';path.write_text('custom content')
        self.assertNotEqual(self.uninstall().returncode,0);self.assertEqual(path.read_text(),'custom content');self.assertTrue(self.current.exists())
    def test_05_unmarked_isolation_refused(self):
        env={**self.env,'KDEGLANCE_TEST_ROOT':str(self.root/'unmarked')}
        result=self.run_command('bash',str(ROOT/'install.sh'),'--test-isolated',env=env)
        self.assertNotEqual(result.returncode,0);self.assertFalse((self.home/'data').exists())
    def test_06_symlinked_integration_refused(self):
        victim=self.home/'keep';victim.write_text('keep')
        path=self.home/'data/applications/io.github.KDEGlance2.desktop';path.parent.mkdir(parents=True);path.symlink_to(victim)
        result=self.run_command('bash',str(ROOT/'install.sh'),'--test-isolated')
        self.assertNotEqual(result.returncode,0);self.assertTrue(path.is_symlink());self.assertEqual(victim.read_text(),'keep')
    def test_07_symlinked_application_root_refused(self):
        victim=self.home/'keep-dir';victim.mkdir();(victim/'keep').write_text('keep')
        target=self.home/'data/kdeglance2';target.parent.mkdir(parents=True);target.symlink_to(victim,target_is_directory=True)
        result=self.run_command('bash',str(ROOT/'install.sh'),'--test-isolated')
        self.assertNotEqual(result.returncode,0);self.assertEqual((victim/'keep').read_text(),'keep')
    def test_08_explicit_preference_purge_is_scoped(self):
        self.install();folder=self.home/'config/kdeglance2';folder.mkdir(parents=True)
        for name in ('audio.ini','video.ini','other.ini'):(folder/name).write_text('keep unless owned')
        self.assertEqual(self.uninstall('--purge-settings').returncode,0)
        self.assertFalse((folder/'audio.ini').exists());self.assertFalse((folder/'video.ini').exists());self.assertTrue((folder/'other.ini').exists())
    def test_09_unsupported_distribution_is_reported(self):
        release=self.root/'os-release';release.write_text('ID=fedora\nPRETTY_NAME="Fedora"\n')
        with patch('check_env.run',side_effect=lambda args:subprocess.CompletedProcess(args,0,('dolphin 26.08.2' if args[-1]=='dolphin' else 'plasma-workspace 6.5.0') if args[:2]==['pacman','-Q'] else '', '')):
            result=check_env.probe(require_session=False,release_file=release)
        self.assertTrue(any('Only Arch' in error for error in result['errors']))
    def test_10_dependency_refusal_is_noninteractive_and_read_only(self):
        bin_dir=self.root/'bin';bin_dir.mkdir()
        pacman=bin_dir/'pacman';pacman.write_text('#!/bin/sh\ncase "$1" in -T) echo gcc; exit 127;; -Q) echo "$2 26.08.2-1";; *) exit 1;; esac\n');pacman.chmod(0o755)
        marker=self.root/'sudo-was-called';sudo=bin_dir/'sudo';sudo.write_text('#!/bin/sh\ntouch "'+str(marker)+'"\nexit 1\n');sudo.chmod(0o755)
        env={**self.env,'PATH':str(bin_dir)+':'+os.environ['PATH']}
        result=self.run_command('bash',str(ROOT/'install.sh'),env=env)
        self.assertNotEqual(result.returncode,0);self.assertFalse(marker.exists());self.assertFalse(self.manifest.exists())
        check=self.run_command('bash',str(ROOT/'install.sh'),'--check',env=env)
        self.assertNotEqual(check.returncode,0);self.assertFalse(marker.exists());self.assertFalse(self.manifest.exists())
    def test_11_existing_foreign_cli_is_not_overwritten(self):
        path=self.home/'.local/bin/kdeglance';path.parent.mkdir(parents=True);path.write_text('unrelated command')
        result=self.run_command('bash',str(ROOT/'install.sh'),'--test-isolated')
        self.assertNotEqual(result.returncode,0);self.assertEqual(path.read_text(),'unrelated command')
    def test_12_missing_manifest_removal_does_not_touch_other_files(self):
        keep=self.home/'keep.txt';keep.write_text('keep')
        self.assertEqual(self.uninstall().returncode,0);self.assertEqual(keep.read_text(),'keep')

if __name__=='__main__':unittest.main(verbosity=2)
