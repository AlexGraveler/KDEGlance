#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Plasma geometry check, synthetic Qt input and media; no user settings edits."""
import argparse,json,os,pathlib,shutil,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--installed',action='store_true');p.add_argument('--record',action='store_true');p.add_argument('--cap',action='store_true',help='Grow photos to the screen cap and shrink back');p.add_argument('--smb',action='append',default=[],help='Remote video URL to exercise (repeatable; never recorded)');p.add_argument('--no-screenshots',action='store_true');args=p.parse_args()
if args.record and args.smb:p.error('Record synthetic media separately from remote files')
root=pathlib.Path(__file__).resolve().parents[2];out=root/'build/desktop/sequence';out.mkdir(parents=True,exist_ok=True)
qdbus=shutil.which('qdbus6') or shutil.which('qdbus-qt6')
def call(s,p,m,*a):return subprocess.check_output([qdbus,s,p,m,*map(str,a)],text=True,stderr=subprocess.PIPE,timeout=12).strip()
def v(m,*a):return call('org.kde.kdeglance','/Viewer',m,*a)
def status():return json.loads(v('Status'))
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   if fn():return True
  except (subprocess.SubprocessError,ValueError):pass
  time.sleep(.05)
 return False
def check(name,ok,detail=''):
 print(('PASS ' if ok else 'FAIL ')+name+' '+str(detail),flush=True)
 if not ok:raise RuntimeError(name)
check('Existing preview closed',not status()['visible'])
v('Quit');time.sleep(.4)
config=out/'config';(config/'kdeglance').mkdir(parents=True,exist_ok=True)
(config/'kdeglance/settings.json').write_text(json.dumps({'schema':1,'general':{'closeOnFocusLoss':False},'window':{'sizes':{'image':{'w':780,'h':520},'video':{'w':780,'h':520}}},'media':{'muted':True},'video':{'muted':True}}))
for name,size in [('landscape','960x640'),('portrait','640x960')]:
 subprocess.run(['magick','-size',size,'gradient:#204080-#f08020',str(out/(name+'.png'))],check=True)
 subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size='+size+':rate=10','-t','3','-c:v','mpeg4',str(out/(name+'.mkv'))],check=True)
env=os.environ.copy();env.update(QT_QPA_PLATFORM='wayland',KDEGLANCE_LANG='en',XDG_CONFIG_HOME=str(config),LD_PRELOAD=str(root/'build/tests/libsessionadapter.so'))
if args.installed:env.pop('KDEGLANCE_KWIN_SCRIPT',None)
else:env['KDEGLANCE_KWIN_SCRIPT']=str(root/'src/kwin/placement.js')
recorder=None
backdrop=None
log=(out/('installed.log' if args.installed else 'build.log')).open('w')
viewer=subprocess.Popen(['/usr/bin/kdeglance' if args.installed else str(root/'build/src/kdeglance'),'--service'],env=env,stdout=log,stderr=log)
def adapter(m,*a):return call('org.kde.kdeglance.Test.p'+str(viewer.pid),'/KDEGlanceTest',m,*a)
def geometry():
 adapter('StoreWindowInfo','[]')
 code='const ws=workspace.windowList().filter(w=>w.pid==='+str(viewer.pid)+');callDBus("org.kde.kdeglance.Test.p'+str(viewer.pid)+'","/KDEGlanceTest","org.kde.kdeglance.TestAdapter","StoreWindowInfo",JSON.stringify(ws.map(w=>({x:w.frameGeometry.x,y:w.frameGeometry.y,width:w.frameGeometry.width,height:w.frameGeometry.height}))));'
 script=out/'snapshot.js';script.write_text(code)
 ident=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',script,'kdeglance-zoom-snapshot')
 try:
  call('org.kde.KWin','/Scripting/Script'+ident,'org.kde.kwin.Script.run')
  check('Geometry reply',wait(lambda:json.loads(adapter('WindowInfo') or '[]'),3))
  return json.loads(adapter('WindowInfo'))[0]
 finally:call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-zoom-snapshot')
try:
 check('Own service',wait(lambda:call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID','org.kde.kdeglance')==str(viewer.pid)))
 check('Adapter',wait(lambda:adapter('State')))
 check('KWin helper',wait(lambda:status()['kwinScript']))
 adapter('StartTrace')
 observer=out/'observer.js'
 observer.write_text('function track(w){if(w.pid!=='+str(viewer.pid)+')return;function record(){const g=w.frameGeometry;callDBus("org.kde.kdeglance.Test.p'+str(viewer.pid)+'","/KDEGlanceTest","org.kde.kdeglance.TestAdapter","RecordGeometry",JSON.stringify({t:Date.now(),event:"compositor",caption:w.caption,x:g.x,y:g.y,width:g.width,height:g.height}));}w.frameGeometryChanged.connect(record);record();}workspace.windowAdded.connect(track);workspace.windowList().forEach(track);')
 observer_id=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',observer,'kdeglance-sequence-observer')
 call('org.kde.KWin','/Scripting/Script'+observer_id,'org.kde.kwin.Script.run')
 if args.record:
  subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',str([(out/'landscape.png').as_uri()]),'0','{}'],check=True,stdout=log)
  check('Recording scene ready',wait(lambda:status().get('presentationReady',False)))
  capture=out/('capture-'+str(int(time.time())));capture.mkdir()
  screen_info=json.loads(adapter('ScreenInfo'))
  screen_info['work']=status()['workArea']
  (capture/'crop.json').write_text(json.dumps(screen_info))
  backdrop=subprocess.Popen([str(root/'build/tests/geometrybackdrop')],stdout=log,stderr=log)
  time.sleep(.5)
  focus=out/'focus.js';focus.write_text('for(const w of workspace.windowList())if(w.pid==='+str(viewer.pid)+')workspace.activeWindow=w;')
  focus_id=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',focus,'kdeglance-sequence-focus')
  call('org.kde.KWin','/Scripting/Script'+focus_id,'org.kde.kwin.Script.run')
  call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-sequence-focus')
  recorder=subprocess.Popen(['python3',str(root/'tests/desktop/record-test-scene.py'),str(capture)],stdout=log,stderr=log)
  print('PORTAL: select the test screen; a neutral backdrop covers the recorded work area.',flush=True)
  check('Portal recording authorized',wait(lambda:(capture/'ready').exists() or recorder.poll() is not None,180) and (capture/'ready').exists())
  focus_id=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',focus,'kdeglance-sequence-focus')
  call('org.kde.KWin','/Scripting/Script'+focus_id,'org.kde.kwin.Script.run')
  call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-sequence-focus')
  time.sleep(.5)
  (capture/'arm').write_text('record')
 for ext in ('png','mkv'):
  for name in ('landscape','portrait'):
   adapter('RecordGeometry',json.dumps({'event':'open','name':name+'.'+ext}))
   subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',str([(out/(name+'.'+ext)).as_uri()]),'0','{}'],check=True,stdout=log)
   def ready():
    s=status();return s.get('presentationReady',True) and (s.get('hasImage',False) if ext=='png' else s.get('hasFrame',False))
   check('Content ready '+name+'.'+ext,wait(ready),status());time.sleep(.3)
   s=status();r=s['imageRect'] if ext=='png' else s['videoRect']
   check('Initial aspect fit '+name+'.'+ext,abs(r[0])<1.1 and abs(r[1])<1.1 and abs(r[2]-s['width'])<1.1 and abs(r[3]-s['height'])<1.1,(s['width'],s['height'],r))
   trace=json.loads(adapter('Trace'));opened=max(i for i,e in enumerate(trace) if e.get('event')=='open')
   paints=[e for e in trace[opened+1:] if e.get('event')==12 and e.get('widget')==('imageView' if ext=='png' else 'videoSurface')]
   check('Every initial content paint already fits '+name+'.'+ext,bool(paints) and all(abs(e['rect'][0])<1.1 and abs(e['rect'][1])<1.1 and abs(e['rect'][2]-e['width'])<1.1 and abs(e['rect'][3]-e['height'])<1.1 for e in paints),len(paints))
   if not args.record and not args.no_screenshots:adapter('Snapshot',out/(name+'-'+ext+'.png'))
   for delta in ((120,)*9+(-120,)*9 if args.cap and ext=='png' else (120,120,-120,-120,3,-3)):
    s=status();g=geometry();r=s['imageRect'] if ext=='png' else s['videoRect']
    x=int(s['width']*.37);y=int(s['height']*.31)
    point=((x-r[0])/r[2],(y-r[1])/r[3]);screen=(g['x']+x,g['y']+y)
    adapter('RecordGeometry',json.dumps({'t':int(time.time()*1000),'event':'input','delta':delta,'name':name+'.'+ext})); before_geometry=g.copy(); adapter('Wheel','imageView' if ext=='png' else 'videoSurface',x,y,delta,'false'); placement=v('PhotoZoomPlacement'); time.sleep(.25)
    s=status();g=geometry();r=s['imageRect'] if ext=='png' else s['videoRect']
    error=abs(g['x']+r[0]+point[0]*r[2]-screen[0])+abs(g['y']+r[1]+point[1]*r[3]-screen[1])
    check('Cursor anchor '+name+'.'+ext+' '+str(delta),error<1.6,{'error':round(error,4)} if error<1.6 else {'error':round(error,4),'before':before_geometry,'after':g,'placement':placement,'rect':r})
    trace=json.loads(adapter('Trace'))
    last_input=max(i for i,e in enumerate(trace) if e.get('event')=='input')
    # Plasma fractional geometry can notify twice for the same rectangle,
    # e.g. 735.1999999999998 then 735.2. Count distinct geometry, not signals.
    changes=[];last_geometry=before_geometry
    for event in trace[last_input+1:]:
     if event.get('event')=='compositor' and any(abs(event[key]-last_geometry[key])>1e-6 for key in ('x','y','width','height')):
      changes.append(event);last_geometry=event
    viewport_changed=abs(g['width']-before_geometry['width'])>0.5 or abs(g['height']-before_geometry['height'])>0.5
    check('Single compositor transaction '+name+'.'+ext+' '+str(delta),len(changes)==(1 if ext=='png' and viewport_changed else 0),len(changes))
   if not args.record: v('Close')
   time.sleep(.1)
 for remote in args.smb:
  def open_url(url):
   subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',str([url]),'0','{}'],check=True,stdout=log)
  started=time.monotonic();open_url(remote)
  check('SMB frame and committed geometry',wait(lambda:status().get('presentationReady',False) and status().get('hasFrame',False),60),{'seconds':round(time.monotonic()-started,2)})
  s=status();check('SMB uses local KIO Fuse path','kio-fuse' in s['localPath'] and not s['localPath'].startswith('smb:'))
  r=s['videoRect'];check('SMB aspect fit',abs(r[0])<1.1 and abs(r[1])<1.1 and abs(r[2]-s['width'])<1.1 and abs(r[3]-s['height'])<1.1)
  open_url((out/'portrait.png').as_uri())
  check('SMB to local switch',wait(lambda:status().get('hasImage',False) and status().get('presentationReady',False)))
  open_url(remote);v('Close');time.sleep(1)
  check('Close during SMB resolve stays closed',not status()['visible'] and not status()['presentationReady'])
 if args.smb:
  open_url(args.smb[0].rsplit('/',1)[0]+'/kdeglance-missing-test-file-4d96.mkv')
  check('Missing remote file shows error card',wait(lambda:status().get('view')=='info' and status().get('presentationReady',False),30))
  v('Close')
finally:
 try:
  (out/('installed-trace.json' if args.installed else 'build-trace.json')).write_text(adapter('Trace'))
  call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-sequence-observer')
  if recorder:
   (capture/'stop').write_text('stop');recorder.wait(timeout=15)
  if backdrop:backdrop.terminate();backdrop.wait(timeout=10)
  v('Quit')
 finally:
  viewer.wait(timeout=10);log.close()
  subprocess.run(['/usr/bin/kdeglance','--status'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
