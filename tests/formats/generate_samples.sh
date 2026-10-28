#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Creates small real sample files for as many supported formats as the tools
# on this machine can write (ffmpeg, ImageMagick, optional cjxl/heif-enc).
# Formats that cannot be produced here are listed in SKIPPED.txt.
#
# usage: generate_samples.sh OUTPUT_DIR
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:?output dir}"
mkdir -p "$OUT"
cd "$OUT" || exit 1
rm -f ./* 2>/dev/null
: > SKIPPED.txt

have() { command -v "$1" >/dev/null 2>&1; }
IM=""
if have magick; then IM="magick"; elif have convert; then IM="convert"; fi
FF=""
have ffmpeg && FF="ffmpeg -nostdin -loglevel error -y"

skip() { echo "$1: $2" >> SKIPPED.txt; }
ff() { # ff <output> <args...>
    local out="$1"; shift
    [ -n "$FF" ] || { skip "$out" "ffmpeg missing"; return; }
    $FF "$@" "$out" 2>/dev/null || { rm -f "$out"; skip "$out" "ffmpeg cannot encode"; }
}
im() { # im <output> <args...>
    local out="$1"; shift
    [ -n "$IM" ] || { skip "$out" "ImageMagick missing"; return; }
    $IM "$@" "$out" 2>/dev/null || { rm -f "$out"; skip "$out" "ImageMagick cannot write"; return; }
    [ -s "$out" ] || { rm -f "$out"; skip "$out" "empty output"; }
}

V="-f lavfi -i testsrc=size=320x240:rate=25 -t 1.5"
A="-f lavfi -i sine=frequency=440:duration=1.5"
AV="-f lavfi -i testsrc=size=320x240:rate=25 -f lavfi -i sine=frequency=440 -t 1.5"
IMG="-f lavfi -i testsrc=size=320x240 -frames:v 1"

# ---------------- images ----------------
ff sample.jpg $IMG
cp -f sample.jpg sample.jfif 2>/dev/null
ff sample.png $IMG
ff sample.apng -f lavfi -i testsrc=size=160x120:rate=5 -t 1 -plays 0 -f apng
ff sample.bmp $IMG
cp -f sample.bmp sample.dib 2>/dev/null
ff sample.gif -f lavfi -i testsrc=size=160x120:rate=5 -t 1
ff sample.tiff $IMG
im multipage.tif -size 64x48 xc:red xc:green xc:blue
ff sample.webp $IMG -c:v libwebp
ff animated.webp -f lavfi -i testsrc=size=160x120:rate=5 -t 1 -c:v libwebp -loop 0
ff sample.tga $IMG -rle 0
ff sample-rle.tga $IMG -rle 1
ff sample.ppm $IMG
ff sample.pgm $IMG -pix_fmt gray
ff sample.pbm $IMG -pix_fmt monob
ff sample.pcx $IMG
ff sample.qoi $IMG
ff sample.jp2 $IMG -c:v libopenjpeg
[ -f sample.jp2 ] || im sample.jp2 -size 64x48 gradient:red-blue
ff sample.hdr $IMG -c:v hdr
ff sample.exr $IMG -c:v exr -pix_fmt gbrpf32le
[ -f sample.exr ] || im sample.exr -size 64x48 gradient:red-blue
im sample.ico -size 48x48 gradient:red-blue
im sample.icns -size 128x128 gradient:red-blue
im sample.psd -size 64x48 gradient:red-blue
ff sample.avif $IMG -c:v libaom-av1 -still-picture 1
[ -f sample.avif ] || ff sample.avif $IMG -c:v libsvtav1
[ -f sample.avif ] || im sample.avif -size 64x48 gradient:red-blue
im sample.heic -size 64x48 gradient:red-blue
if have cjxl && [ -f sample.png ]; then cjxl -q 90 sample.png sample.jxl >/dev/null 2>&1 || skip sample.jxl "cjxl failed"; else im sample.jxl -size 64x48 gradient:red-blue; fi
cat > sample.svg <<'SVG'
<svg xmlns="http://www.w3.org/2000/svg" width="120" height="80"><rect width="120" height="80" fill="#3daee9"/><circle cx="60" cy="40" r="25" fill="#fff"/></svg>
SVG
gzip -c sample.svg > sample.svgz

if have python3; then
    python3 "$SCRIPT_DIR/generate-dng.py" synthetic.dng || { rm -f synthetic.dng; skip synthetic.dng "DNG generator failed"; }
else
    skip synthetic.dng "python3 missing"
fi

# ---------------- video ----------------
ff video-h264.mp4 $AV -c:v libx264 -pix_fmt yuv420p -c:a aac
[ -f video-h264.mp4 ] || ff video-h264.mp4 $AV -c:v mpeg4 -c:a aac
ff video-hevc.mp4 $V -c:v libx265 -pix_fmt yuv420p -tag:v hvc1
ff video-av1.mkv $V -c:v libaom-av1 -cpu-used 8
[ -f video-av1.mkv ] || ff video-av1.mkv $V -c:v libsvtav1
ff video.m4v $V -c:v mpeg4 -f mp4
ff video-vp9.mkv $AV -c:v libvpx-vp9 -deadline realtime -c:a libopus
ff video-vp8.webm $AV -c:v libvpx -deadline realtime -c:a libvorbis
ff video-prores.mov $V -c:v prores_ks -profile:v 0
ff video.qt $V -c:v mjpeg -f mov
ff video-dnxhd.mov -f lavfi -i testsrc=size=1280x720:rate=25 -t 0.5 -c:v dnxhd -b:v 90M -pix_fmt yuv422p
ff video-xvid.avi $AV -c:v mpeg4 -vtag xvid -c:a mp3
ff video.divx $V -c:v mpeg4 -vtag DIVX -f avi
ff video-mjpeg.avi $V -c:v mjpeg
ff video.wmv $AV -c:v wmv2 -c:a wmav2
ff video-vc1-like.asf $V -c:v wmv1 -f asf
ff video.mpg $AV -c:v mpeg1video -c:a mp2
ff video.mpeg $V -c:v mpeg2video
ff video.mpe $V -c:v mpeg1video -f mpeg
ff video.vob $AV -c:v mpeg2video -c:a ac3 -f vob
ff video.ts $AV -c:v mpeg2video -c:a mp2 -f mpegts
ff video.mts $V -c:v libx264 -pix_fmt yuv420p -f mpegts
ff video.m2ts $V -c:v mpeg2video -f mpegts
ff video.flv $AV -c:v flv1 -c:a mp3
ff video.f4v $V -c:v libx264 -pix_fmt yuv420p -f f4v
ff video.3gp -f lavfi -i testsrc=size=176x144:rate=15 -t 1.5 -c:v h263 -f 3gp
ff video.3g2 -f lavfi -i testsrc=size=176x144:rate=15 -t 1.5 -c:v mpeg4 -f 3g2
ff video.ogv $V -c:v libtheora
ff video.mxf $V -c:v mpeg2video -pix_fmt yuv422p -f mxf
ff video.dv -f lavfi -i testsrc=size=720x576:rate=25 -t 0.5 -c:v dvvideo -pix_fmt yuv420p -f dv
ff video.rm $AV -c:v rv20 -c:a real_144 -ar 8000 -ac 1 -f rm
cp -f video.rm video.rmvb 2>/dev/null

# ---------------- audio ----------------
ff audio.mp3 $A -c:a libmp3lame -metadata title="Sample" -metadata artist="KDEGlance"
ff audio.mp2 $A -c:a mp2
ff audio.flac $A
ff audio.wav $A
cp -f audio.wav audio.wave 2>/dev/null
ff audio.aiff $A -c:a pcm_s16be -f aiff
cp -f audio.aiff audio.aif 2>/dev/null
ff audio-alac.m4a $A -c:a alac
ff audio.m4a $A -c:a aac
cp -f audio.m4a audio.m4b 2>/dev/null
ff audio.aac $A -c:a aac -f adts
ff audio.ogg $A -c:a libvorbis
ff audio.opus $A -c:a libopus
ff audio.wma $A -c:a wmav2 -f asf
ff audio.wv $A -c:a wavpack
ff audio.tta $A -c:a tta
ff audio.ac3 $A -c:a ac3
ff audio.eac3 $A -c:a eac3
ff audio.dts $A -c:a dca -strict -2 -ar 48000 -ac 2
ff audio.amr -f lavfi -i sine=frequency=440:duration=1.5 -ar 8000 -ac 1 -c:a libopencore_amrnb
ff audio.au $A -c:a pcm_mulaw -f au
cp -f audio.au audio.snd 2>/dev/null
ff audio.ra -f lavfi -i sine=frequency=440:duration=1.5 -ar 8000 -ac 1 -c:a real_144 -f rm
for f in ape mpc tak shn mod xm s3m it mp1; do skip "audio.$f" "no free encoder available to create a sample"; done

# MIDI (one note) and a RealMedia metafile.
printf 'MThd\0\0\0\006\0\0\0\001\0\140MTrk\0\0\0\014\0\220\074\100\140\200\074\0\0\377\057\0' > audio.mid
printf 'rtsp://example.org/stream.rm\nhttp://example.org/fallback.ra\n' > stream.ram

ls -1 | grep -v SKIPPED.txt | wc -l | xargs echo "samples:"
