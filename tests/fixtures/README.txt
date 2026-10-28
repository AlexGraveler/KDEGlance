av1.mkv: synthetic 64x48 RGB test pattern, 2 AV1 frames, generated locally:
ffmpeg -f lavfi -i testsrc=size=64x48:rate=5 -t 0.4 -c:v libaom-av1 -cpu-used 8 av1.mkv
Used by the production decoder preflight and the regression test. Contains no user media.
