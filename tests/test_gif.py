# SPDX-License-Identifier: GPL-3.0-or-later
#!/usr/bin/env python3
"""Private GUI-session tests using generated, redistributable GIFs only."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from PIL import Image

ROOT=Path(__file__).resolve().parent.parent/'kdeglance'
sys.path.insert(0,str(ROOT))
from PyQt6.QtCore import QThreadPool, QSize
from PyQt6.QtGui import QImage, QImageReader
from PyQt6.QtWidgets import QApplication
from PyQt6.QtTest import QTest
from animation import AnimationStream
from loader import LoadJob, MAX_ANIMATION_BYTES, MAX_FRAMES
from window import PreviewWindow
from daemon import Controller

from manage import validate_test_environment
validate_test_environment()
app=QApplication(['gif-tests']);app.setQuitOnLastWindowClosed(False)


def wait(predicate,seconds=8):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        QTest.qWait(5)
        if predicate():return
    raise AssertionError('timeout')


def drain(window,count=None,seconds=15):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        window.advance_frame();window.timer.stop()
        if count is not None and window.animation_displayed>=count:return
        if window.animation and window.animation.snapshot()['finished'] and not window.animation.snapshot()['buffered_frames']:return
        QTest.qWait(1)
    raise AssertionError('drain timeout')


class Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(dir=os.environ['KDEGLANCE_TEST_ROOT']);cls.root=Path(cls.temp.name)
        cls.finite=cls.root/'finite.gif'
        cls.colors=['red','green','blue','yellow','magenta']
        images=[Image.new('RGB',(64,32),color) for color in cls.colors]
        images[0].save(cls.finite,save_all=True,append_images=images[1:],duration=[30,40,50,60,70],loop=2,optimize=False,disposal=2)
        cls.long=cls.root/'long.gif'
        palette=[value for i in range(256) for value in (i,255-i,i//2)]
        images=[]
        for i in range(MAX_FRAMES+5):
            image=Image.new('P',(2,2));image.putpalette(palette)
            image.putpixel((0,0),i%256);image.putpixel((1,0),(i//256+1)%256)
            images.append(image)
        images[0].save(cls.long,save_all=True,append_images=images[1:],duration=20,loop=0,optimize=False,disposal=2)
        cls.sample=cls.root/'memory-bounded.gif'
        frames=[]
        for i in range(200):
            frame=Image.new('P',(540,540));frame.putpalette(palette)
            frame.paste(i%256,(0,0,540,540));frames.append(frame)
        frames[0].save(cls.sample,save_all=True,append_images=frames[1:],duration=20,loop=0,optimize=False,disposal=2)

    @classmethod
    def tearDownClass(cls):
        QThreadPool.globalInstance().waitForDone(5000)
        cls.temp.cleanup()

    def window(self):
        w=PreviewWindow(lambda:None,lambda _:None);w.show()
        def cleanup():w.hide();w.clear_content('');w.deleteLater();QTest.qWait(20)
        self.addCleanup(cleanup)
        return w

    def test_01_more_than_2000_frames_and_bounded_queue(self):
        with Image.open(self.long) as image:self.assertEqual(image.n_frames,MAX_FRAMES+5)
        result=LoadJob(1,str(self.long),[str(self.long)]).decode()
        self.assertIn('animation_path',result)
        self.assertEqual(len(result['frames']),1)
        w=self.window();w.set_frames(result['frames'],result['loops'],result['animation_path'],result['animation_frame_count'])
        w.timer.stop();stream=w.animation
        drain(w,MAX_FRAMES+5)
        self.assertGreaterEqual(w.animation_displayed,MAX_FRAMES+5)
        self.assertLessEqual(stream.snapshot()['buffered_frames'],3)
        self.assertLessEqual(stream.snapshot()['peak_buffer_bytes'],32*1024*1024)
        self.assertFalse(stream.snapshot()['error'])
        drain(w,MAX_FRAMES+7)
        self.assertGreater(w.completed_loops,0)

    def test_02_finite_loop_count_delays_and_transform_persistence(self):
        with patch('loader.MAX_FRAMES',2):result=LoadJob(1,str(self.finite),[str(self.finite)]).decode()
        w=self.window();w.set_frames(result['frames'],result['loops'],result['animation_path'])
        w.timer.stop();w.rotate_image();w.flip_image();w.canvas.zoom=2
        wait(lambda:w.animation.snapshot()['buffered_frames']>0)
        w.advance_frame();w.timer.stop()
        self.assertEqual(w.frames[0][1],40)
        self.assertEqual(w.canvas.image.size(),QSize(32,64))
        self.assertEqual(w.image_rotation,270);self.assertTrue(w.image_mirrored);self.assertEqual(w.canvas.zoom,2)
        drain(w)
        self.assertEqual(w.animation_displayed,15)
        self.assertTrue(w.animation.snapshot()['finished'])
        self.assertFalse(w.animation.snapshot()['error'])

    def test_03_backpressure_cancellation_and_hide(self):
        stream=AnimationStream(str(self.finite),QSize(64,32),-1)
        QThreadPool.globalInstance().start(stream)
        wait(lambda:stream.snapshot()['buffered_frames']==3)
        stream.cancel();wait(lambda:stream.snapshot()['finished'])
        self.assertEqual(stream.snapshot()['buffered_frames'],0)
        self.assertEqual(stream.snapshot()['buffer_bytes'],0)
        self.assertIsNone(stream.take())
        result=LoadJob(1,str(self.long),[str(self.long)]).decode()
        w=self.window();w.set_frames(result['frames'],result['loops'],result['animation_path'])
        old=w.animation;w.hide()
        wait(lambda:old.snapshot()['finished'])
        self.assertIsNone(w.animation)
        self.assertFalse(w.timer.isActive())

    def test_04_worker_error_is_visible_and_small_gif_unchanged(self):
        result=LoadJob(1,str(self.finite),[str(self.finite)]).decode()
        self.assertEqual(len(result['frames']),5)
        self.assertNotIn('animation_path',result)
        w=self.window();errors=[];w.animationError.connect(errors.append)
        w.set_frames(result['frames'][:1],0,str(self.root/'missing.gif'))
        wait(lambda:bool(errors))
        self.assertFalse(w.frames)
        self.assertTrue(w.canvas.message)

    def test_05_large_generated_gif_full_quality_all_frames_and_loop(self):
        with Image.open(self.sample) as image:
            count=image.n_frames;size=QSize(*image.size)
            self.assertGreater(image.width*image.height*count*4,MAX_ANIMATION_BYTES)
        c=Controller(app)
        try:
            ticks=[]
            from PyQt6.QtCore import QTimer
            timer=QTimer();timer.timeout.connect(lambda:ticks.append(1));timer.start(5)
            c.request(__import__('json').dumps({'paths':[str(self.sample)]}),False)
            wait(lambda:c.phase in ('ready','error'))
            self.assertEqual(c.phase,'ready',c.error)
            w=c.window;self.assertIsNotNone(w.animation)
            self.assertEqual(w.canvas.image.size(),size)
            reference=QImageReader(str(self.sample)).read()
            self.assertEqual(w.frames[0][0],reference)
            w.timer.stop();drain(w,count+2)
            self.assertGreater(w.completed_loops,0)
            self.assertLessEqual(w.animation.snapshot()['peak_buffer_bytes'],3*size.width()*size.height()*4)
            self.assertGreater(len(ticks),10)
            blue=self.root/'blue.png';image=QImage(20,10,QImage.Format.Format_RGB32);image.fill(0xff0000ff);image.save(str(blue))
            old=w.animation
            c.request(__import__('json').dumps({'paths':[str(blue)]}),False)
            wait(lambda:c.phase=='ready')
            wait(lambda:old.snapshot()['finished'])
            self.assertIsNone(w.animation)
            QTest.qWait(80)
            self.assertEqual(w.canvas.image.pixelColor(0,0).blue(),255)
            c.close_view();timer.stop()
        finally:
            c.close_view();c.pool.waitForDone(5000);c.window.deleteLater();c.deleteLater()


if __name__=='__main__':unittest.main(verbosity=2)
