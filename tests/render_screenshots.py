#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture real widgets and decoded media offscreen, with synthetic fixtures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
ROOT=Path(__file__).resolve().parent.parent
if '--session' not in sys.argv:
    work=ROOT/'.work';work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='screens-',dir=work,ignore_cleanup_errors=True) as directory:
        root=Path(directory);(root/'.kdeglance-test-root').touch()
        env={**os.environ,'HOME':directory,'XDG_RUNTIME_DIR':directory,'KDEGLANCE_TEST_ROOT':directory,
             'XDG_CONFIG_HOME':str(root/'config'),'XDG_DATA_HOME':str(root/'data'),
             'XDG_CACHE_HOME':str(root/'cache'),'XDG_STATE_HOME':str(root/'state'),
             'QT_QPA_PLATFORM':'offscreen','PYTHONDONTWRITEBYTECODE':'1'}
        sys.exit(subprocess.call(['dbus-run-session','--',sys.executable,'-B',__file__,'--session'],env=env))
sys.path.insert(0,str(ROOT/'kdeglance'))
from manage import validate_test_environment
validate_test_environment()
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import QPoint,QPointF,Qt
from PyQt6.QtGui import QImage,QWheelEvent
from PyQt6.QtTest import QTest
from window import PreviewWindow
from audio import AudioWindow
from video import VideoWindow
app=QApplication(['kdeglance-screenshots']);app.setQuitOnLastWindowClosed(False)
output=ROOT/'screenshots';output.mkdir(exist_ok=True)
def wait(predicate,seconds=10):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        QTest.qWait(10)
        if predicate():return
    raise RuntimeError('Media/widget capture timed out')
def controls(window,visible):
    window.interactions.fade.stop();window.interactions.idle.stop()
    window.interactions.set_opacity(1 if visible else 0)
    app.processEvents()
def capture(window,name):
    assert window.grab().save(str(output/name))
photo=PreviewWindow(lambda:None,lambda _:None)
image=QImage(str(next((ROOT/'kdeglance/examples').glob('*.png'))))
photo.present(app.primaryScreen());photo.set_frames([(image,100)],0)
photo.resize_to_image(image,app.primaryScreen());QTest.qWait(50)
controls(photo,False);capture(photo,'image-preview.png')
point=QPointF(photo.canvas.rect().center())
wheel=QWheelEvent(point,point,QPoint(),QPoint(0,120),Qt.MouseButton.NoButton,Qt.KeyboardModifier.NoModifier,Qt.ScrollPhase.NoScrollPhase,False)
app.sendEvent(photo.canvas,wheel);controls(photo,True);capture(photo,'image-controls.png')
photo.hide();photo.clear_content('')
audio=AudioWindow(lambda:None,lambda _:None);audio.output.setMuted(True);audio.output.setVolume(0)
for filename,screenshot in [('preview.mp3','audio-cover.png'),('preview.wav','audio-placeholder.png')]:
    audio.present(app.primaryScreen());audio.open_audio(str(ROOT/'kdeglance/examples/audio'/filename))
    wait(lambda:audio.phase=='error' or (audio.player.duration()>0 and audio.player.position()>100))
    if audio.error:raise RuntimeError(audio.error)
    if filename.endswith('.mp3'):wait(lambda:not audio.canvas.image.isNull())
    else:assert audio.canvas.image.isNull()
    audio.player.setPosition(1500);audio.player.pause();QTest.qWait(80)
    controls(audio,True);capture(audio,screenshot)
    audio.stop_audio();audio.hide()
video=VideoWindow(lambda:None,lambda _:None);video.output.setMuted(True);video.output.setVolume(0)
video.present(app.primaryScreen());video.open_video(str(ROOT/'kdeglance/examples/video/preview.mp4'))
wait(lambda:video.phase=='error' or video.frames_received>3)
if video.error:raise RuntimeError(video.error)
video.player.pause();controls(video,True);capture(video,'video-preview.png')
video.stop_video();video.hide();audio.metadata_pool.waitForDone(5000)
for window in (photo,audio,video):window.deleteLater()
app.processEvents()
print('Captured five real QWidget/media previews with original generated fixtures.')
