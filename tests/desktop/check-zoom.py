#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Plasma geometry check, synthetic Qt input and media; no user settings edits."""
import argparse,json,os,pathlib,shutil,subprocess,time
p=argparse.ArgumentParser();p.add_argument('--installed',action='store_true');p.add_argument('--no-screenshots',action='store_true');args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[2];out=root/'build/desktop/zoom';out.mkdir(parents=True,exist_ok=True)
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
 for ext in ('png','mkv'):
  for name in ('landscape','portrait'):
   subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',str([(out/(name+'.'+ext)).as_uri()]),'0','{}'],check=True,stdout=log)
   def ready():
    s=status();return s.get('hasImage',False) if ext=='png' else s.get('hasFrame',False)
   check('Content ready '+name+'.'+ext,wait(ready));time.sleep(.3)
   s=status();r=s['imageRect'] if ext=='png' else s['videoRect']
   check('Initial aspect fit '+name+'.'+ext,abs(r[0])<1.1 and abs(r[1])<1.1 and abs(r[2]-s['width'])<1.1 and abs(r[3]-s['height'])<1.1,(s['width'],s['height'],r))
   if not args.no_screenshots:adapter('Snapshot',out/(name+'-'+ext+'.png'))
   for delta in (120,120,-120,-120,3,-3):
    s=status();g=geometry();r=s['imageRect'] if ext=='png' else s['videoRect']
    x=int(s['width']*.37);y=int(s['height']*.31)
    point=((x-r[0])/r[2],(y-r[1])/r[3]);screen=(g['x']+x,g['y']+y)
    before_geometry=g.copy(); adapter('Wheel','imageView' if ext=='png' else 'videoSurface',x,y,delta,'false'); placement=v('PhotoZoomPlacement'); time.sleep(.25)
    s=status();g=geometry();r=s['imageRect'] if ext=='png' else s['videoRect']
    error=abs(g['x']+r[0]+point[0]*r[2]-screen[0])+abs(g['y']+r[1]+point[1]*r[3]-screen[1])
    check('Cursor anchor '+name+'.'+ext+' '+str(delta),error<1.6,{'error':round(error,4)} if error<1.6 else {'error':round(error,4),'before':before_geometry,'after':g,'placement':placement,'rect':r})
   v('Close');time.sleep(.1)
finally:
 try:v('Quit')
 finally:
  viewer.wait(timeout=10);log.close()
  subprocess.run(['/usr/bin/kdeglance','--status'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15)
