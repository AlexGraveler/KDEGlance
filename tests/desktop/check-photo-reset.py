#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Actual Wayland windows, Qt event injection; resize proposals are not physical seat drags.
import json,os,pathlib,shutil,subprocess,time
root=pathlib.Path(__file__).resolve().parents[2];out=pathlib.Path(os.environ.get('KDEGLANCE_TEST_OUTPUT',str(root/'build/desktop/photo-state')));out.mkdir(parents=True,exist_ok=True)
samples=root/'build/desktop/candidate-samples';report=[];viewer=None;dolphin=None;stopped=False
binary=os.environ.get('KDEGLANCE_TEST_BINARY',str(root/'build/src/kdeglance'))
shotdir=os.environ.get('KDEGLANCE_RELEASE_SCREENSHOT_DIR')
if shotdir:pathlib.Path(shotdir).mkdir(parents=True,exist_ok=True)
def call(s,p,m,*args):
 r=subprocess.run(['qdbus6',s,p,m,*map(str,args)],capture_output=True,text=True,timeout=15)
 if r.returncode:raise RuntimeError(f'{s} {m}: {r.returncode} {r.stdout!r} {r.stderr!r}')
 return r.stdout.strip()
def v(m,*args):return call('org.kde.kdeglance','/Viewer',m,*args)
def status():return json.loads(v('Status'))
def wait(pred,timeout=15):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  if pred():return True
  time.sleep(.05)
 return False
def check(n,c,d=''):
 line=('PASS ' if c else 'FAIL ')+n+' '+str(d);print(line,flush=True);report.append(line)
 if not c:raise RuntimeError(n)
def adapter(pid,m,*args):return call('org.kde.kdeglance.Test.p'+str(pid),'/KDEGlanceTest',m,*args)
def script(code):
 path=out/'window.js';path.write_text(code)
 ident=call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.loadScript',path,'kdeglance-photo-reset-test')
 try:call('org.kde.KWin','/Scripting/Script'+ident,'org.kde.kwin.Script.run');time.sleep(.15)
 finally:call('org.kde.KWin','/Scripting','org.kde.kwin.Scripting.unloadScript','kdeglance-photo-reset-test')
def geometry(x=None,y=None):
 adapter(viewer.pid,'StoreWindowInfo','[]')
 move='' if x is None else 'for(const w of ws){const g=w.frameGeometry;w.frameGeometry={x:'+str(x)+',y:'+str(y)+',width:g.width,height:g.height};}'
 script('const ws=workspace.windowList().filter(w=>w.pid==='+str(viewer.pid)+');'+move+'callDBus('+json.dumps('org.kde.kdeglance.Test.p'+str(viewer.pid))+',"/KDEGlanceTest","org.kde.kdeglance.TestAdapter","StoreWindowInfo",JSON.stringify(ws.map(w=>({x:w.frameGeometry.x,y:w.frameGeometry.y,width:w.frameGeometry.width,height:w.frameGeometry.height,maximizable:w.maximizable,electricBorderSetter:typeof w.setElectricBorderMode,unrestrictedMoveSetter:typeof w.setUnrestrictedInteractiveMoveResize}))));')
 rows=json.loads(adapter(viewer.pid,'WindowInfo') or '[]');return rows[0] if rows else {}
def open_url(url):
 subprocess.run(['gdbus','call','--session','-d','org.kde.kdeglance','-o','/Viewer','-m','org.kde.kdeglance.Viewer.Open',json.dumps([url]),'0','{}'],check=True,capture_output=True)
 return wait(lambda:status()['windowShown'])
def full_image():
 s=status();r=s['imageRect'];return abs(r[0])<1 and abs(r[1])<1 and abs(r[2]-s['width'])<1.1 and abs(r[3]-s['height'])<1.1
config=out/'config';(config/'kdeglance').mkdir(parents=True,exist_ok=True)
settingsfile=config/'kdeglance/settings.json'
sizes={'video':{'w':900,'h':600},'audio':{'w':450,'h':450},'pdf':{'w':610,'h':720},'text':{'w':700,'h':650},'office':{'w':660,'h':800}}
settingsfile.write_text(json.dumps({'schema':1,'general':{'syncSelection':True,'closeOnFocusLoss':False},'window':{'sizes':sizes},'media':{'muted':True,'volume':.33},'audio':{'volume':.25,'muted':True},'video':{'volume':.6,'muted':True},'image':{'rawPreferEmbeddedPreview':False}}))
env=os.environ.copy();env.update(QT_QPA_PLATFORM='wayland',XDG_CONFIG_HOME=str(config),KDEGLANCE_LANG='en',KDEGLANCE_KWIN_SCRIPT=str(root/'src/kwin/placement.js'),LD_PRELOAD=str(root/'build/tests/libsessionadapter.so'))
if binary.startswith(str(root/'build')):
 env['QT_PLUGIN_PATH']=os.environ.get('KDEGLANCE_TEST_PLUGIN_PATH',str(root/'build/plugins'))
 env['KDEGLANCE_KWIN_SCRIPT']=os.environ.get('KDEGLANCE_TEST_KWIN_SCRIPT',str(root/'src/kwin/placement.js'))
else:
 env.pop('QT_PLUGIN_PATH',None);env.pop('KDEGLANCE_KWIN_SCRIPT',None)
log=open(out/'application.log','w')
try:
 check('Existing preview is closed',not status()['visible']);v('Quit');stopped=True;time.sleep(.5)
 def start():
  p=subprocess.Popen([binary,'--service'],env=env,stdout=log,stderr=log)
  check('Adapter registered',wait(lambda:subprocess.run(['qdbus6','org.kde.kdeglance.Test.p'+str(p.pid),'/KDEGlanceTest','State'],capture_output=True).returncode==0))
  check('Own viewer service',wait(lambda:subprocess.run(['qdbus6','org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus.GetConnectionUnixProcessID','org.kde.kdeglance'],capture_output=True,text=True).stdout.strip()==str(p.pid)))
  return p
 viewer=start()
 nav=out/'KDEGlancePhotoResetNavigation';nav.mkdir(exist_ok=True)
 for src,dst in [('sample.jpg','01-image.jpg'),('long-video.mp4','02-video.mp4'),('unicode.txt','03-text.txt')]:shutil.copy2(samples/src,nav/dst)
 dolphin=subprocess.Popen(['dolphin','--new-window','--select',str(nav/'01-image.jpg')],env=env,stdout=log,stderr=log)
 check('Dolphin bridge registered',wait(lambda:subprocess.run(['qdbus6','org.kde.dolphin-'+str(dolphin.pid),'/KDEGlanceBridge','State'],capture_output=True).returncode==0))
 script('for(const w of workspace.windowList())if(w.pid==='+str(dolphin.pid)+')workspace.activeWindow=w;')
 adapter(dolphin.pid,'FocusFileView');adapter(dolphin.pid,'Key','Space')
 check('Space opens natural image',wait(lambda:status().get('hasImage')) and (status()['width'],status()['height'],status()['zoom'])==(320,240,1),status())
 adapter(viewer.pid,'Wheel','imageView',160,120,-120,'false')
 check('Wheel down shrinks photo and window',status()['width']<320 and full_image(),status())
 check('Wheel does not save geometry','image' not in status()['savedSizes'])
 adapter(viewer.pid,'Wheel','imageView',120,90,120,'false');check('Wheel up enlarges without bars',status()['width']>=319 and full_image(),status())
 check('Free resize proposal applied',adapter(viewer.pid,'ResizePreview',600,600,'false')=='true')
 s=status();r=s['imageRect'];check('Free square preserves image proportions and bars',s['width']==600 and s['height']==600 and abs(r[2]/r[3]-4/3)<.001 and r[1]>0,s)
 adapter(viewer.pid,'Snapshot',out/'free-square.png')
 check('Alt resize proposal applied',adapter(viewer.pid,'ResizePreview',800,650,'true')=='true')
 check('Alt uses photo ratio from a square',status()['width']==800 and status()['height']==600 and full_image(),status())
 adapter(viewer.pid,'Snapshot',out/'alt-photo-ratio.png')
 adapter(viewer.pid,'Wheel','imageView',300,250,-120,'false')
 check('Wheel after resize remains temporary',status()['width']<800 and status()['savedSizes']['image']==[800,600] and full_image(),status())
 adapter(viewer.pid,'Key','Esc');check('Escape closes',wait(lambda:not status()['visible']))
 v('Quit');viewer.wait(timeout=8);viewer=start();open_url((nav/'01-image.jpg').as_uri());wait(lambda:status().get('hasImage'))
 check('Restart restores manual size and fresh fit',status()['width']==800 and status()['height']==600 and status()['fit'] and full_image(),status())
 time.sleep(.3)
 geometry(-200,-100);time.sleep(.3);g=geometry();check('Photo remains partly offscreen',g.get('x')==-200 and g.get('y')==-100,g)
 adapter(viewer.pid,'Wheel','imageView',300,250,-120,'false');time.sleep(.3);g=geometry();check('Compositor resize remains stable',g==geometry(),g)
 check('Alt drag requests compositor move',adapter(viewer.pid,'Drag','imageView','true')=='1')
 # Crop beyond the window limit, then verify an off-centre local anchor.
 geometry(100,100)
 adapter(viewer.pid,'Wheel','imageView',100,100,1440,'false');time.sleep(.2)
 before=status();cursor=(min(900,before['width']-100),min(500,before['height']-100))
 point=[(cursor[i]-before['imageRect'][i])/before['zoom'] for i in range(2)]
 adapter(viewer.pid,'Wheel','imageView',*cursor,120,'false');after=status()
 check('Zoom continues past window limit',after['zoom']>before['zoom'] and (after['width'],after['height'])==(before['width'],before['height']),after)
 check('Cursor anchor at capped window',sum(abs(after['imageRect'][i]+point[i]*after['zoom']-cursor[i]) for i in range(2))<.01)
 fixed=geometry();before=after
 check('Ordinary drag does not request window move',adapter(viewer.pid,'Drag','imageView','false')=='0')
 after=status();check('Ordinary drag changes only local image offset',after['imageRect'][:2]!=before['imageRect'][:2] and after['zoom']==before['zoom'] and geometry()==fixed,after)
 before=after
 check('Alt cropped drag requests window move',adapter(viewer.pid,'Drag','imageView','true')=='1')
 check('Alt drag preserves local image',status()['imageRect']==before['imageRect'] and status()['zoom']==before['zoom'])
 wheel_start=time.monotonic()
 for i in range(20):
  adapter(viewer.pid,'Wheel','imageView',*cursor,-15,'false');adapter(viewer.pid,'Wheel','imageView',*cursor,15,'false')
 check('Rapid direction reversals preserve scale',abs(status()['zoom']-before['zoom'])<1e-8,{'seconds':time.monotonic()-wheel_start})
 # Exercise genuine KWin state transitions for this process only.
 def window_action(action):
  script('for(const w of workspace.windowList())if(w.pid==='+str(viewer.pid)+'){workspace.activeWindow=w;'+action+';}')
 def inside():
  g=geometry();a=status()['workArea'];return g.get('x',-9999)>=a[0]-1 and g.get('y',-9999)>=a[1]-1 and g['x']+g['width']<=a[0]+a[2]+1 and g['y']+g['height']<=a[1]+a[3]+1
 saved=status()['savedSizes'];window_action('w.setMaximize(true,true)');time.sleep(.4)
 check('KWin maximize stays in work area',inside(),geometry())
 fixed=geometry();adapter(viewer.pid,'Wheel','imageView',500,400,-120,'false');time.sleep(.2)
 check('Wheel leaves maximized geometry to KWin',geometry()==fixed,geometry())
 window_action('w.setMaximize(false,false)');time.sleep(.4)
 for side in ('Left','Right'):
  window_action('workspace.slotWindowQuickTile'+side+'()');time.sleep(.4)
  check('Tiling '+side+' keeps photo',status()['view']=='image',status());check('KWin '+side+' tile stays visible',inside(),geometry())
  fixed=geometry();adapter(viewer.pid,'Wheel','imageView',500,400,-120,'false');time.sleep(.2)
  check('Wheel leaves '+side+' tile geometry to KWin',geometry()==fixed,geometry())
  window_action('w.tile=null');time.sleep(.3);check('Leaving '+side+' tile keeps photo',status()['view']=='image',status())
 check('Compositor transitions preserve manual geometry',status()['savedSizes']==saved,status()['savedSizes'])
 window_action('w.setMaximize(true,true)');time.sleep(.3);v('Close')
 open_url((nav/'01-image.jpg').as_uri());wait(lambda:status().get('hasImage'));time.sleep(.2)
 check('Reopen after maximize restores manual size',status()['width']==800 and status()['height']==600 and full_image(),status())
 geometry(100,100)
 def reset(name=''):
  s=status();adapter(viewer.pid,'MiddleDouble',name,s['width']//2,s['height']//2)
 check('Current file remains photo after compositor transitions',status()['view']=='image',status());before=json.loads(settingsfile.read_text());reset('imageView');wait(lambda:status()['width']==320)
 check('Photo reset restores standard natural size',status()['width']==320 and status()['height']==240 and status()['zoom']==1 and status()['imageRotation']==0,status())
 after=json.loads(settingsfile.read_text());check('Photo reset preserves all other geometry',after['window']['sizes']==sizes,after['window']['sizes'])
 for key in ('general','media','audio','video'):check('Photo reset preserves '+key,after[key]==before[key])
 check('Photo-specific defaults restored',after['image']['rawPreferEmbeddedPreview'])
 v('Close')
 for filename,kind,category in [('long-video.mp4','media','video'),('audio.wav','media','audio'),('sample.pdf','pdf','pdf'),('example.md','text','text'),('sample.fodt','office','office')]:
  file=samples/filename
  if category=='audio' and not file.exists():
   file=out/'sound.wav';subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','sine=duration=2',str(file)],check=True)
  check('Open '+category,open_url(file.as_uri()))
  check('Module ready '+category,wait(lambda:status()['view']==kind and (status().get('cachedPages',0)>0 if kind in ('pdf','office') else status().get('loaded',True)),40))
  if category=='video':
   controls=json.loads(adapter(viewer.pid,'Inspect'))
   check('Video Repeat button is visible','repeatPlayback:Repeat' in controls['buttons'],controls)
   adapter(viewer.pid,'Click','repeatPlayback',8,8);check('Video Repeat toggles on',status()['loop'])
   adapter(viewer.pid,'Click','repeatPlayback',8,8);check('Video Repeat toggles off',not status()['loop'])
  if shotdir:
   shot={'video':'05-video.png','pdf':'09-pdf.png','text':'10-text.png','office':'11-office.png'}.get(category)
   if shot:adapter(viewer.pid,'Snapshot',pathlib.Path(shotdir)/shot)
  if category=='video':adapter(viewer.pid,'Key','R');adapter(viewer.pid,'Wheel','videoSurface',100,30,120,'false')
  if category in ('pdf','office'):adapter(viewer.pid,'Wheel','pdfCanvas',100,100,120,'true')
  if category=='text':adapter(viewer.pid,'Key','M')
  before=json.loads(settingsfile.read_text());reset();time.sleep(.15);after=json.loads(settingsfile.read_text())
  expected=dict(before['window']['sizes']);expected.pop(category,None)
  check('Reset only active geometry '+category,after['window']['sizes']==expected,after['window']['sizes'])
  for other in ('image','audio','video','pdf','text','office','general','media'):
   if other!=category:check(category+' preserves '+other,after.get(other)==before.get(other))
  if category in ('audio','video'):check('Media reset defaults '+category,status()['volume']==.8 and not status()['muted'],status())
  if category=='video':check('Video transform reset',status()['videoRotation']==0 and status()['videoZoom']==1,status())
  if category in ('pdf','office'):check('PDF presentation reset '+category,not status()['reflow'] and status()['width']==800 and status()['height']==700,status())
  v('Close')
 expected=json.loads(settingsfile.read_text());v('Quit');viewer.wait(timeout=8);viewer=start();open_url((nav/'01-image.jpg').as_uri());wait(lambda:status().get('hasImage'))
 check('All independent settings survive restart',json.loads(settingsfile.read_text())==expected)
 reset('imageView');v('Close');open_url((nav/'01-image.jpg').as_uri());wait(lambda:status().get('hasImage'))
 before=status();geometry(350,250);after=status()
 check('Reset reopen move preserves local image',before['zoom']==after['zoom'] and before['imageRect']==after['imageRect'])
 adapter(viewer.pid,'Wheel','imageView',100,80,1920,'false');time.sleep(.2)
 before=status();p=(900,500);point=[(p[i]-before['imageRect'][i])/before['zoom'] for i in range(2)]
 adapter(viewer.pid,'Wheel','imageView',*p,120,'false');after=status()
 check('Reset reopen move zoom keeps cursor anchor',sum(abs(after['imageRect'][i]+point[i]*after['zoom']-p[i]) for i in range(2))<.01)
 v('Close');open_url((nav/'01-image.jpg').as_uri());wait(lambda:status().get('hasImage'))
 check('No temporary photo zoom after module resets',status()['zoom']==1 and status()['width']==320,status());v('Close')
 for i in range(8):
  open_url((samples/['sample.jpg','unicode.txt','sample.pdf','long-video.mp4'][i%4]).as_uri());adapter(viewer.pid,'Key','Esc');check('Mixed close cycle '+str(i+1),not status()['visible'] and not status().get('playing',False))
 check('Service remains resident',viewer.poll() is None);v('Ping')
except Exception as error:
 import traceback;traceback.print_exc();report.append('FAIL driver '+str(error))
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
