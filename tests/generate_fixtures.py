#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate redistributable media from geometric artwork, color bars and sine waves.

No downloaded photographs, songs or external sample clips are used.
Requires Pillow, FFmpeg (including the test codecs) and heif-enc.
"""
import base64
import io
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'kdeglance/examples'
sys.path.insert(0,str(ROOT/'kdeglance/vendor'))

def command(args):
    subprocess.run(args,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=120)

def ffmpeg(args):
    command(['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-y',*args])

def main():
    OUT.mkdir(exist_ok=True)
    (OUT/'audio').mkdir(exist_ok=True);(OUT/'video').mkdir(exist_ok=True)
    image=Image.new('RGB',(960,540))
    pixels=image.load()
    for y in range(540):
        for x in range(960):pixels[x,y]=(int(30+50*x/960),int(55+100*y/540),int(100+90*x/960))
    draw=ImageDraw.Draw(image)
    draw.ellipse((630,55,860,285),fill=(244,202,140))
    draw.polygon([(0,420),(260,150),(580,540),(0,540)],fill=(35,88,104))
    draw.polygon([(250,540),(620,225),(960,540)],fill=(25,65,84))
    picture=OUT/'01 — KDEGlance [тест] %20.png';image.save(picture)
    with tempfile.TemporaryDirectory(prefix='fixture-',dir=ROOT/'.work') as directory:
        temporary=Path(directory)
        small=Image.new('RGB',(120,80),(240,20,20));ImageDraw.Draw(small).rectangle((60,0,119,79),fill=(0,220,40))
        for source,name in [(small,'02 — HEIC [тест].heic'),(small.rotate(90,expand=True),'03 — HEIC rotated.heic')]:
            png=temporary/(name+'.png');source.save(png)
            command(['heif-enc','-q','95','-p','x265:log-level=error','-p','x265:pools=1',str(png),'-o',str(OUT/name)])
    audio=OUT/'audio';wave=audio/'preview.wav'
    ffmpeg(['-f','lavfi','-i','sine=frequency=440:sample_rate=16000:duration=12','-c:a','pcm_s16le',str(wave)])
    codecs={'mp3':['-c:a','libmp3lame','-b:a','48k'],'flac':['-c:a','flac'],
            'ogg':['-c:a','libvorbis'],'m4a':['-c:a','aac','-b:a','48k'],'aac':['-c:a','aac','-b:a','48k']}
    for extension,args in codecs.items():ffmpeg(['-i',str(wave),*args,str(audio/('preview.'+extension))])
    from mutagen.id3 import ID3,APIC
    from mutagen.flac import FLAC,Picture
    from mutagen.oggvorbis import OggVorbis
    from mutagen.mp4 import MP4,MP4Cover
    data=picture.read_bytes();tags=ID3();tags.add(APIC(mime='image/png',type=3,desc='Original geometric test artwork',data=data));tags.save(audio/'preview.mp3')
    cover=Picture();cover.type=3;cover.mime='image/png';cover.width=960;cover.height=540;cover.data=data
    track=FLAC(audio/'preview.flac');track.add_picture(cover);track.save()
    track=OggVorbis(audio/'preview.ogg');track['metadata_block_picture']=[base64.b64encode(cover.write()).decode()];track.save()
    track=MP4(audio/'preview.m4a');track['covr']=[MP4Cover(data,imageformat=MP4Cover.FORMAT_PNG)];track.save()
    (audio/'broken.mp3').write_bytes(b'not an audio file')
    video=OUT/'video';mp4=video/'preview.mp4'
    ffmpeg(['-f','lavfi','-i','testsrc2=size=640x360:rate=15:duration=12','-f','lavfi','-i','sine=frequency=330:sample_rate=48000:duration=12','-c:v','libx264','-preset','ultrafast','-crf','28','-threads','1','-pix_fmt','yuv420p','-c:a','aac','-b:a','64k','-movflags','+faststart','-shortest',str(mp4)])
    for extension in ('mkv','mov'):ffmpeg(['-i',str(mp4),'-c','copy',str(video/('preview.'+extension))])
    variants={'webm':['-c:v','libvpx-vp9','-deadline','realtime','-cpu-used','8','-c:a','libopus'],
              'avi':['-c:v','mpeg4','-q:v','8','-c:a','libmp3lame'],
              'wmv':['-c:v','wmv2','-b:v','400k','-c:a','wmav2'],
              'flv':['-c:v','flv','-b:v','400k','-c:a','libmp3lame','-ar','44100'],
              'rmvb':['-c:v','rv20','-b:v','300k','-c:a','real_144','-ar','8000','-ac','1','-f','rm']}
    for extension,args in variants.items():ffmpeg(['-i',str(mp4),*args,'-threads','1',str(video/('preview.'+extension))])
    ffmpeg(['-display_rotation:v:0','90','-i',str(mp4),'-c','copy',str(video/'rotated.mp4')])
    for name,size in [('portrait-silent.mp4','240x400'),('large-silent.mp4','1920x1080')]:
        ffmpeg(['-f','lavfi','-i','testsrc2=size='+size+':rate=15:duration=12','-c:v','libx264','-preset','ultrafast','-crf','32','-threads','1','-pix_fmt','yuv420p',str(video/name)])
    (video/'broken.mp4').write_bytes(b'not a video file')
    print('Generated original image/audio/video fixtures; no external media used.')

if __name__=='__main__':
    (ROOT/'.work').mkdir(exist_ok=True)
    main()
