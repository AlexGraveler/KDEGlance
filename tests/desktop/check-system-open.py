#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Actual user MIME associations, real Wayland application windows, synthetic keys.
import json,os,pathlib,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[2];out=pathlib.Path(os.environ.get('KDEGLANCE_TEST_OUTPUT',str(root/'build/desktop/system-open')));out.mkdir(parents=True,exist_ok=True)
report=[]
def call(s,p,m,*args):
 r=subprocess.run(['qdbus6',s,p,m,*map(str,args)],capture_output=True,text=True,timeout=15)
 if r.returncode:raise RuntimeError(f'{s} {m}: {r.returncode} {r.stdout!r} {r.stderr!r}')
 return r.stdout.strip()
def v(m,*args):return call('org.kde.kdeglance','/Viewer',m,*args)
def status():return json.loads(v('Status'))
def adapter(m,*args):return call('org.kde.kdeglance.Test.p'+str(viewer.pid),'/KDEGlanceTest',m,*args)
def wait(pred,timeout=20):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if pred():return True
  time.sleep(.2)
 return False
def check(n,c,d=''):
 line=('PASS ' if c else 'FAIL ')+n+' '+str(d);print(line,flush=True);report.append(line)
 if not c:raise RuntimeError(n)
def script(code):
 path=out/'windows.js';path.write_text(code)
 ident=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',path,'kdeglance-system-open-check')
 try:call('org.kde.KWin','/Scripting/Script'+ident,'org.kde.kwin.Script.run');time.sleep(.15)
 finally:call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-system-open-check')
def appwindows(marker,application,close=False):
 adapter('StoreWindowInfo','[]')
 predicate='w.pid!=='+str(viewer.pid)+' && w.caption.includes('+json.dumps(marker)+') && String(w.resourceClass).toLowerCase().includes('+json.dumps(application)+')'
 script('const ws=workspace.windowList().filter(w=>'+predicate+');callDBus('+json.dumps('org.kde.kdeglance.Test.p'+str(viewer.pid))+',"/KDEGlanceTest","org.kde.kdeglance.TestAdapter","StoreWindowInfo",JSON.stringify(ws.map(w=>({pid:w.pid,caption:w.caption,app:String(w.resourceClass)}))));'+('for(const w of ws)w.closeWindow();' if close else ''))
 return json.loads(adapter('WindowInfo') or '[]')
def open_url(url):
 subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',json.dumps([url]),'0','{}'],check=True,capture_output=True)
 if not wait(lambda:status()['windowShown']):return False
 script('for(const w of workspace.windowList())if(w.pid==='+str(viewer.pid)+')workspace.activeWindow=w;')
 return wait(lambda:status()['windowShown'])
viewer=None;stopped=False;owned=[];log=open(out/'application.log','w')
env=os.environ.copy();env.update(QT_QPA_PLATFORM='wayland',KDEGLANCE_LANG='en',LD_PRELOAD=str(root/'build/tests/libsessionadapter.so'))
# No XDG_CONFIG/DATA override here: exercise the real user's application choices.
try:
 check('User preview is closed',not status()['visible'])
 for app in ('gwenview','kate','vlc','haruna','okular','soffice.bin'):
  check('No existing '+app+' process',subprocess.run(['pgrep','-x',app],capture_output=True).returncode!=0)
 v('Quit');stopped=True;time.sleep(.5)
 viewer=subprocess.Popen([os.environ.get('KDEGLANCE_TEST_BINARY',str(root/'build/src/kdeglance')),'--service'],env=env,stdout=log,stderr=log)
 check('Test source service registered',wait(lambda:subprocess.run(['qdbus6','org.kde.kdeglance.Test.p'+str(viewer.pid),'/KDEGlanceTest','State'],capture_output=True).returncode==0))
 image=out/'KDEGlanceEnterImage-20261028.png';shutil.copy2(root/'build/desktop/candidate-samples/sample.png',image)
 text=out/'KDEGlanceEnterText-20261028.txt';shutil.copy2(root/'build/desktop/candidate-samples/unicode.txt',text)
 smb=os.environ.get('KDEGLANCE_TEST_SMB_URL')
 if not smb:print('NOT RUN SMB: no authorized test URL supplied',flush=True)
 video=out/'KDEGlanceEnterVideo-20261028.mp4';shutil.copy2(root/'build/desktop/candidate-samples/long-video.mp4',video)
 cases=[(image.as_uri(),'image/png',image.name,'Return'),(text.as_uri(),'text/plain',text.name,'Enter'),(video.as_uri(),'video/mp4',video.name,'Return')]
 if smb:cases.append((smb,'video/3gpp',pathlib.PurePosixPath(smb).name,'Enter'))
 print('Audio/PDF/office dispatch is covered separately with isolated associations; this runner opens only the selected image/text/video GUI applications.',flush=True)
 for url,mime,marker,key in cases:
  association=subprocess.run(['xdg-mime','query','default',mime],capture_output=True,text=True,check=True).stdout.strip()
  check('Real default '+mime,bool(association),association)
  app=association.removesuffix('.desktop').split('.')[-1].lower()
  if app not in ('gwenview','kate','vlc','haruna'):
   raise RuntimeError('GUI matcher has no safe window ownership rule for '+app+'; not launching it')
  check('Preview '+app,open_url(url))
  if url==smb:check('SMB preview decoded',wait(lambda:status().get('videoFrames',0)>2,30))
  adapter('Key',key);check('Enter closes preview before '+app,wait(lambda:not status()['visible'] and not status().get('playing',False)))
  owned.append((marker,app))
  check('Real default application window '+app,wait(lambda:bool(appwindows(marker,app)),30))
  rows=appwindows(marker,app);check('Exactly one test window '+app,len(rows)==1,rows)
  check('Service alive after '+app,viewer.poll() is None);v('Ping')
  appwindows(marker,app,True);time.sleep(.6)
except Exception as error:
 import traceback;traceback.print_exc();report.append('FAIL driver '+str(error))
finally:
 if viewer:
  for marker,app in owned:
   try:appwindows(marker,app,True)
   except Exception:pass
  try:v('Quit');viewer.wait(timeout=8)
  except Exception:pass
 if stopped:
  time.sleep(.5)
  try:v('Ping')
  except Exception:pass
 log.close();(out/'results.txt').write_text('\n'.join(report)+'\n')
raise SystemExit(1 if any(line.startswith('FAIL') for line in report) else 0)
