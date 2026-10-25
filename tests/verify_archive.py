#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract safely, then install/start/use/remove only inside disposable HOME."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
ROOT=Path(__file__).resolve().parent.parent

def session(source,examples):
    root=Path(os.environ['KDEGLANCE_TEST_ROOT'])
    assert (root/'.kdeglance-test-root').is_file()
    def run(*args):return subprocess.run(args,cwd=root,check=True,capture_output=True,text=True,timeout=45)
    run('bash',str(source/'install.sh'),'--test-isolated')
    client=Path.home()/'.local/bin/kdeglance'
    assert run(str(client),'--version').stdout.strip()=='KDEGlance 1.0.1'
    current=Path(os.environ['XDG_DATA_HOME'])/'kdeglance2/current'
    log=(root/'daemon.log').open('w')
    daemon=subprocess.Popen([sys.executable,'-B',str(current/'daemon.py')],stdout=log,stderr=log)
    def state():return json.loads(run(str(client),'state').stdout)
    def wait(condition):
        end=time.monotonic()+12
        while time.monotonic()<end:
            try:
                value=state()
                if condition(value):return value
            except (subprocess.SubprocessError,ValueError):pass
            time.sleep(.05)
        raise RuntimeError('Packaged daemon did not reach expected state')
    try:
        # Query the bus itself first, so GetState cannot race and autoactivate another daemon.
        deadline=time.monotonic()+10
        while time.monotonic()<deadline:
            owner=run('busctl','--user','call','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','NameHasOwner','s','io.github.KDEGlance2').stdout.strip()
            if owner=='b true':break
            if daemon.poll() is not None:raise RuntimeError('Packaged daemon exited before registration')
            time.sleep(.05)
        value=wait(lambda s:s['version']=='1.0.1')
        assert value['pid']==daemon.pid, 'Unexpected daemon in private test session'
        for path,module in [(next(examples.glob('*.png')),'image'),(examples/'audio/preview.mp3','audio'),(examples/'video/preview.mp4','video')]:
            run(str(client),'open','--',str(path))
            value=wait(lambda s:s.get('module')==module and s.get('visible') and (s.get('phase')=='ready' if module=='image' else (s.get('video',{}).get('frames_received',0)>2 if module=='video' else s.get('audio_playing',s.get('playing',False)) or s.get('audio',{}).get('playing',False))))
            assert value['version']=='1.0.1'
        run(str(client),'close')
    finally:
        daemon.terminate();daemon.wait(timeout=10);log.close()
    run('bash',str(source/'uninstall.sh'),'--test-isolated')
    run('bash',str(source/'uninstall.sh'),'--test-isolated')
    assert not current.exists()
    print('PASS: extracted archive install, CLI version, private D-Bus image/audio/video playback, close, uninstall twice.')

def main():
    if len(sys.argv)>1 and sys.argv[1]=='--session':return session(Path(sys.argv[2]),Path(sys.argv[3]))
    archive=ROOT/'KDEGlance-1.0.1.tar.gz'
    checksum=archive.with_name(archive.name+'.sha256').read_text().split()[0]
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==checksum
    sys.path.insert(0,str(ROOT/'packaging'))
    from distribution import archive_files
    expected=archive_files()
    work=ROOT/'.work';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='archive-',dir=work) as directory:
        root=Path(directory);(root/'.kdeglance-test-root').touch()
        extracted=root/'source with spaces — исходники';extracted.mkdir()
        seen=set()
        with tarfile.open(archive,'r:gz') as stream:
            for member in stream:
                parts=Path(member.name).parts
                assert member.isfile() and parts[0]=='KDEGlance-1.0.1' and '..' not in parts
                assert not Path(member.name).is_absolute()
                assert not any(p in ('.work','.git','__pycache__','.venv','venv') for p in parts)
                assert not member.name.endswith(('.so','.pyc','.log','.tar.gz'))
                name='/'.join(parts[1:])
                assert name in expected and name not in seen,name
                seen.add(name)
                assert stream.extractfile(member).read()==expected[name].read_bytes(),name
                target=extracted/member.name;target.parent.mkdir(parents=True,exist_ok=True)
                target.write_bytes(stream.extractfile(member).read());target.chmod(member.mode)
        source=extracted/'KDEGlance-1.0.1'
        assert seen==set(expected),'Distribution allowlist mismatch'
        assert not (source/'tests').exists() and not (source/'screenshots').exists()
        assert not (source/'kdeglance/examples').exists()
        home=root/'home with spaces — пользователь';home.mkdir()
        env={**os.environ,'HOME':str(home),'KDEGLANCE_TEST_ROOT':str(root),
             'XDG_RUNTIME_DIR':str(root),'XDG_DATA_HOME':str(home/'data'),'XDG_CONFIG_HOME':str(home/'config'),
             'XDG_STATE_HOME':str(home/'state'),'XDG_CACHE_HOME':str(home/'cache'),
             'QT_QPA_PLATFORM':'offscreen','PYTHONDONTWRITEBYTECODE':'1'}
        subprocess.run(['dbus-run-session','--',sys.executable,'-B',str(Path(__file__).resolve()),'--session',str(source),str(ROOT/'kdeglance/examples')],env=env,check=True,timeout=75)
    print('PASS: archive checksum, safe structure, minimal allowlist/source bytes, no test media, Unicode/spaces extraction.')

if __name__=='__main__':main()
