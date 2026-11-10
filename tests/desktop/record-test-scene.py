#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Record a portal-authorized test screen; the driver supplies a neutral backdrop."""
import os,pathlib,subprocess,sys,time,uuid
import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
DBusGMainLoop(set_as_default=True)
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
bus=dbus.SessionBus();portal=bus.get_object('org.freedesktop.portal.Desktop','/org/freedesktop/portal/desktop')
responses={};loop=GLib.MainLoop()
def response(code,result,path=None):responses[path]=(int(code),result);loop.quit()
bus.add_signal_receiver(response,signal_name='Response',dbus_interface='org.freedesktop.portal.Request',path_keyword='path')
def request(method,*args):
 token='kg'+uuid.uuid4().hex
 opts=dbus.Dictionary({'handle_token':token},signature='sv')
 if method=='CreateSession':opts['session_handle_token']='kg'+uuid.uuid4().hex
 if method=='SelectSources':opts.update(types=dbus.UInt32(1),multiple=False,cursor_mode=dbus.UInt32(2))
 path=str(getattr(dbus.Interface(portal,'org.freedesktop.portal.ScreenCast'),method)(*args,opts))
 deadline=time.monotonic()+180
 while path not in responses and time.monotonic()<deadline:
  GLib.timeout_add(250,lambda:(loop.quit(),False)[1]);loop.run()
 if path not in responses:raise RuntimeError('Portal selection timed out')
 code,result=responses.pop(path)
 if code:raise RuntimeError('Portal denied/cancelled recording: '+str(code))
 return result
session=request('CreateSession')['session_handle']
try:
 request('SelectSources',session)
 result=request('Start',session,'')
 node=int(result['streams'][0][0])
 fd=dbus.Interface(portal,'org.freedesktop.portal.ScreenCast').OpenPipeWireRemote(session,dbus.Dictionary({},signature='sv')).take()
 import gi,json
 gi.require_version('Gst','1.0')
 from gi.repository import Gst
 from PIL import Image
 Gst.init(None)
 crop=json.loads((out/'crop.json').read_text());screen=crop['screen'];work=crop['work']
 pipeline=Gst.parse_launch('pipewiresrc fd='+str(fd)+' path='+str(node)+' do-timestamp=true ! videoconvert ! video/x-raw,format=RGB ! appsink name=frames emit-signals=true max-buffers=3 drop=true')
 count=[0];metadata=(out/'frames.jsonl').open('w')
 def sample(sink):
  sample=sink.emit('pull-sample');caps=sample.get_caps().get_structure(0);w=caps.get_value('width');h=caps.get_value('height')
  buffer=sample.get_buffer();ok,info=buffer.map(Gst.MapFlags.READ)
  if ok:
   if not (out/'ready').exists():(out/'ready').write_text('ready')
   if not (out/'arm').exists():
    buffer.unmap(info)
    return Gst.FlowReturn.OK
   stride=len(info.data)//h
   image=Image.frombytes('RGB',(w,h),info.data,'raw','RGB',stride)
   sx=w/screen[2];sy=h/screen[3]
   box=(round((work[0]-screen[0])*sx),round((work[1]-screen[1])*sy),round((work[0]-screen[0]+work[2])*sx),round((work[1]-screen[1]+work[3])*sy))
   image.crop(box).save(out/('frame-%06d.png'%count[0]),compress_level=1)
   metadata.write(json.dumps({'frame':count[0],'t':time.time_ns()/1e6,'pts':int(buffer.pts),'width':w,'height':h})+'\n');metadata.flush()
   buffer.unmap(info);count[0]+=1
  return Gst.FlowReturn.OK
 pipeline.get_by_name('frames').connect('new-sample',sample)
 pipeline.set_state(Gst.State.PLAYING)
 try:
  while not (out/'stop').exists():
   while GLib.MainContext.default().pending():GLib.MainContext.default().iteration(False)
   time.sleep(.02)
 finally:
  pipeline.set_state(Gst.State.NULL);metadata.close()

finally:
 try:dbus.Interface(bus.get_object('org.freedesktop.portal.Desktop',session),'org.freedesktop.portal.Session').Close()
 except dbus.DBusException:pass
