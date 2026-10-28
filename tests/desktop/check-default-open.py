#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Real Wayland module Enter/KIO dispatch, synthetic Qt input; isolated associations.
import json,os,pathlib,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(os.environ.get('KDEGLANCE_TEST_OUTPUT',str(root/'build/desktop/default-open')));out.mkdir(parents=True,exist_ok=True)
samples=root/'build/desktop/candidate-samples'
report=[]
def call(service,path,method,*args):
 r=subprocess.run(['qdbus6',service,path,method,*map(str,args)],capture_output=True,text=True,timeout=15)
 if r.returncode:raise RuntimeError(f"{service} {method}: exit={r.returncode}, stdout={r.stdout!r}, stderr={r.stderr!r}")
 return r.stdout.strip()
def v(method,*args):return call('org.kde.kdeglance','/Viewer',method,*args)
def status():return json.loads(v('Status'))
def wait(pred,timeout=15):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if pred():return True
  time.sleep(.05)
 return False
def check(name,ok,detail=''):
 line=('PASS ' if ok else 'FAIL ')+name+' '+str(detail);report.append(line);print(line,flush=True)
 if not ok:raise RuntimeError(name)
def adapter(pid,method,*args):
 if viewer is not None and pid==viewer.pid and method in ('Key','Click','Wheel','Snapshot'):focus(pid)
 return call('org.kde.kdeglance.Test.p'+str(pid),'/KDEGlanceTest',method,*args)
def focus(pid):
 selector='w.pid==='+str(pid)
 if dolphin is not None and pid==dolphin.pid:
  selector+=' || (String(w.resourceClass).includes("dolphin") && w.caption.includes("KDEGlanceFinalActionsNavigation"))'
 script=out/'focus.js';script.write_text('const ws=workspace.windowList().filter(w=>'+selector+');for(const w of ws)workspace.activeWindow=w;callDBus('+json.dumps('org.kde.kdeglance.Test.p'+str(viewer.pid))+',"/KDEGlanceTest","org.kde.kdeglance.TestAdapter","StoreWindowInfo",JSON.stringify(ws.map(w=>({pid:w.pid,caption:w.caption,active:w.active}))));')
 ident=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',script,'kdeglance-final-actions-focus')
 try:call('org.kde.KWin','/Scripting/Script'+ident,'org.kde.kwin.Script.run');time.sleep(.15)
 finally:call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-final-actions-focus')
def open_url(url):
 subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',json.dumps([url]),'0','{}'],check=True,capture_output=True)
 if not wait(lambda:status()['windowShown']):return False
 focus(viewer.pid)
 return wait(lambda:status()['windowShown'])
config=out/'config';data=out/'data';cache=out/'cache'
(config/'kdeglance').mkdir(parents=True,exist_ok=True);(data/'applications').mkdir(parents=True,exist_ok=True)
(config/'kdeglance/settings.json').write_text(json.dumps({'schema':1,'general':{'closeOnFocusLoss':False},'media':{'muted':True},'image':{'zoom':8},'window':{'sizes':{'image':{'w':600,'h':400}}}}))
captures=out/'captured.jsonl';captures.write_text('')
capture=out/'capture.py';capture.write_text("import json,sys\nwith open(sys.argv[1],'a') as f:f.write(json.dumps(sys.argv[2:])+'\\n')\n")
mimes=['image/jpeg','image/png','video/mp4','video/3gpp','audio/vnd.wave','audio/x-wav','audio/wav','text/plain','application/pdf','application/msword','application/vnd.openxmlformats-officedocument.wordprocessingml.document','application/vnd.ms-word.document.macroEnabled.12','application/rtf','text/rtf','application/vnd.oasis.opendocument.text','application/vnd.oasis.opendocument.text-template','application/vnd.oasis.opendocument.text-flat-xml']
(data/'applications/kdeglance-test-capture.desktop').write_text('[Desktop Entry]\nType=Application\nName=KDEGlance test capture\nNoDisplay=true\nTerminal=false\nExec=/usr/bin/python "'+str(capture)+'" "'+str(captures)+'" %U\nMimeType='+';'.join(mimes)+';\nX-KDE-Protocols=file,smb\n')
(config/'mimeapps.list').write_text('[Default Applications]\n'+''.join(m+'=kdeglance-test-capture.desktop;\n' for m in mimes))
env=os.environ.copy();env.update(QT_QPA_PLATFORM='wayland',QT_PLUGIN_PATH=str(root/'build/plugins'),XDG_CONFIG_HOME=str(config),XDG_DATA_HOME=str(data),XDG_CACHE_HOME=str(cache),KDEGLANCE_LANG='en',LD_PRELOAD=str(root/'build/tests/libsessionadapter.so'))
subprocess.run(['update-desktop-database',str(data/'applications')],env=env,check=True,capture_output=True)
subprocess.run(['kbuildsycoca6','--noincremental'],env=env,check=True,capture_output=True)
log=open(out/'application.log','w');viewer=None;dolphin=None;stopped=False
try:
 check('Existing user preview is closed',not status()['visible'])
 v('Quit');stopped=True;time.sleep(.5)
 def start():
  proc=subprocess.Popen([os.environ.get('KDEGLANCE_TEST_BINARY',str(root/'build/src/kdeglance')),'--service'],env=env,stdout=log,stderr=log)
  check('Test service registered',wait(lambda:subprocess.run(['qdbus6','org.kde.kdeglance.Test.p'+str(proc.pid),'/KDEGlanceTest','State'],capture_output=True).returncode==0))
  check('Source owns viewer D-Bus name',wait(lambda:subprocess.run(['qdbus6','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID','org.kde.kdeglance'],capture_output=True,text=True).stdout.strip()==str(proc.pid)))
  return proc
 viewer=start()
 for i,(filename,kind) in enumerate([('sample.jpg','image'),('long-video.mp4','media'),('audio.wav','media'),('unicode.txt','text'),('sample.pdf','pdf'),('sample.doc','office'),('sample.docx','office'),('sample.docm','office'),('sample.rtf','office'),('sample.odt','office'),('sample.ott','office'),('sample.fodt','office')]):
  file=samples/filename
  if filename=='audio.wav' and not file.exists():file=samples/'audio-wav.wav'
  if not file.exists() and filename=='audio.wav':
   file=out/'sound.wav';subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=duration=2',str(file)],check=True)
  url=file.as_uri();check('Open '+filename,open_url(url))
  if kind=='office':check('Office ready '+filename,wait(lambda:status().get('cachedPages',0)>0,40))
  if kind=='media':check('Media loaded '+filename,wait(lambda:status().get('loaded',False)))
  shots=os.environ.get('KDEGLANCE_RELEASE_SCREENSHOT_DIR')
  if shots:
   shot={'unicode.txt':'10-text.png','sample.pdf':'09-pdf.png','sample.fodt':'11-office.png'}.get(filename)
   if shot:adapter(viewer.pid,'Snapshot',pathlib.Path(shots)/shot)
  before=len(captures.read_text().splitlines());key='Return' if i%2==0 else 'Enter'
  adapter(viewer.pid,'Key',key)
  check(key+' closes '+filename,wait(lambda:not status()['visible'] and not status().get('playing',False)))
  check('KIO dispatch '+filename,wait(lambda:len(captures.read_text().splitlines())==before+1))
  args=json.loads(captures.read_text().splitlines()[-1]);check('Original file '+filename,len(args)==1 and (args[0]==url or pathlib.Path(args[0])==file),args)
  v('Ping')
 check('Resident service survived all module Enter launches',viewer.poll() is None);v('Ping')
except Exception as error:
 import traceback;traceback.print_exc()
 try: print('DIAGNOSTIC viewer',status(),flush=True)
 except Exception:pass
 report.append('FAIL driver '+str(error));print('FAIL driver',error,flush=True)
finally:
 if dolphin:
  try:adapter(dolphin.pid,'Quit')
  except Exception:pass
 if viewer:
  try:v('Quit');viewer.wait(timeout=8)
  except Exception:pass
 if stopped:
  time.sleep(.5)
  try:v('Ping')
  except Exception:pass
 log.close();(out/'results.txt').write_text('\n'.join(report)+'\n')
raise SystemExit(1 if any(line.startswith('FAIL') for line in report) else 0)
