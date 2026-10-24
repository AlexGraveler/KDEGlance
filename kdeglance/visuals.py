# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolution-independent reference icons and the empty audio artwork."""
from PyQt6.QtCore import Qt, QPointF, QRectF
from PyQt6.QtGui import QColor, QLinearGradient, QRadialGradient, QPainter, QPainterPath, QPen

ICON_COLOR = '#f7f8fa'


def draw_icon(painter, kind, rect, playing=False):
    painter.save()
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.translate(rect.center())
    scale = min(rect.width(), rect.height())/24
    painter.scale(scale, scale)
    color = QColor(ICON_COLOR)
    pen = QPen(color, 2.3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
    painter.setPen(Qt.PenStyle.NoPen)
    painter.setBrush(color)
    if kind == 'play':
        if playing:
            painter.drawRoundedRect(QRectF(-7,-10,5,20),1.4,1.4)
            painter.drawRoundedRect(QRectF(2,-10,5,20),1.4,1.4)
        else:
            shape=QPainterPath()
            shape.moveTo(-6.5,-10)
            shape.quadTo(-8,-10.8,-8,-8.8)
            shape.lineTo(-8,8.8)
            shape.quadTo(-8,10.8,-6.5,10)
            shape.lineTo(9,1.3)
            shape.quadTo(11,0,9,-1.3)
            shape.closeSubpath()
            painter.drawPath(shape)
    elif kind == 'volume':
        shape=QPainterPath()
        shape.moveTo(-10.3,-4)
        shape.lineTo(-6,-4)
        shape.lineTo(.5,-9.8)
        shape.quadTo(2,-11,2,-8.7)
        shape.lineTo(2,8.7)
        shape.quadTo(2,11,.5,9.8)
        shape.lineTo(-6,4)
        shape.lineTo(-10.3,4)
        shape.quadTo(-11.5,4,-11.5,2.8)
        shape.lineTo(-11.5,-2.8)
        shape.quadTo(-11.5,-4,-10.3,-4)
        painter.drawPath(shape)
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        waves=QPainterPath()
        waves.moveTo(5.3,-4.5);waves.cubicTo(8.1,-2,8.1,2,5.3,4.5)
        waves.moveTo(8.8,-8.7);waves.cubicTo(13.7,-3.8,13.7,3.8,8.8,8.7)
        painter.drawPath(waves)
    elif kind == 'flip':
        shape=QPainterPath()
        shape.moveTo(-3.8,-8.8);shape.quadTo(-3.8,-9.7,-4.6,-8.8)
        shape.lineTo(-11.2,-.8);shape.quadTo(-12,0,-11.2,.8)
        shape.lineTo(-4.6,8.8);shape.quadTo(-3.8,9.7,-3.8,8.8);shape.closeSubpath()
        shape.moveTo(3.8,-8.8);shape.quadTo(3.8,-9.7,4.6,-8.8)
        shape.lineTo(11.2,-.8);shape.quadTo(12,0,11.2,.8)
        shape.lineTo(4.6,8.8);shape.quadTo(3.8,9.7,3.8,8.8);shape.closeSubpath()
        painter.drawPath(shape)
        painter.setPen(pen)
        painter.drawLine(QPointF(0,-11),QPointF(0,11))
    elif kind == 'rotate':
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        arrow=QPainterPath()
        arrow.moveTo(-7.8,5.1)
        arrow.cubicTo(-4.8,10.1,2.5,10.4,6.5,6.1)
        arrow.cubicTo(12.6,-.1,7.8,-10.8,-.3,-9)
        arrow.cubicTo(-4.1,-8.5,-6.3,-7.1,-8.6,-5)
        painter.drawPath(arrow)
        head=QPainterPath()
        head.moveTo(-6.4,-10.5);head.lineTo(-9,-4.8);head.lineTo(-2.7,-4.8)
        painter.drawPath(head)
    elif kind == 'fullscreen':
        painter.setPen(pen)
        painter.setBrush(Qt.BrushStyle.NoBrush)
        for x in (-1,1):
            for y in (-1,1):
                corner=QPainterPath(QPointF(x*3.5,y*9))
                corner.lineTo(x*9,y*9);corner.lineTo(x*9,y*3.5)
                painter.drawPath(corner)
    painter.restore()


def draw_audio_placeholder(painter, rect):
    """Soft glass tile with a white beamed note; fill the existing cover area."""
    painter.save()
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    edge=min(rect.width(),rect.height())
    # A soft backdrop also fills the rounded tile's corners at the original size.
    base=QLinearGradient(rect.topLeft(),rect.bottomRight())
    base.setColorAt(0,QColor('#d6e6ee'))
    base.setColorAt(.45,QColor('#c6d6e1'))
    base.setColorAt(1,QColor('#e5d9df'))
    painter.fillRect(rect,base)
    tile=rect.adjusted(edge*.008,edge*.008,-edge*.008,-edge*.008)
    clip=QPainterPath();clip.addRoundedRect(tile,edge*.15,edge*.15)
    painter.setClipPath(clip)
    for x,y,color in ((.25,.3,QColor(255,255,255,135)),(.25,.65,QColor(128,206,218,120)),
                      (.78,.78,QColor(224,166,190,120)),(.7,.25,QColor(155,175,211,95))):
        glow=QRadialGradient(QPointF(rect.left()+rect.width()*x,rect.top()+rect.height()*y),edge*.57)
        glow.setColorAt(0,color);color.setAlpha(0);glow.setColorAt(1,color)
        painter.fillRect(rect,glow)
    painter.setClipping(False)
    sheen=QLinearGradient(tile.topLeft(),tile.bottomRight())
    sheen.setColorAt(0,QColor(255,255,255,225));sheen.setColorAt(.5,QColor(255,255,255,90));sheen.setColorAt(1,QColor(255,255,255,190))
    painter.setPen(QPen(sheen,max(1,edge*.012)))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    painter.drawRoundedRect(tile,edge*.15,edge*.15)
    painter.translate(rect.left(),rect.top())
    painter.scale(rect.width()/100,rect.height()/100)
    note=QPainterPath()
    note.moveTo(43,30);note.lineTo(70,24);note.quadTo(74,23.3,74,28)
    note.lineTo(74,63);note.cubicTo(74,69.5,65,74.2,59.3,70.8)
    note.cubicTo(52,66.2,56.8,57.5,65.5,58)
    note.lineTo(66.5,58.1);note.lineTo(66.5,37);note.lineTo(47.5,41.5)
    note.lineTo(47.5,70);note.cubicTo(47.5,77,38.5,81,32.5,77)
    note.cubicTo(26,73.2,30,64,39.2,64.3)
    note.lineTo(40.5,64.4);note.lineTo(40.5,34);note.quadTo(40.5,30.5,43,30)
    note.closeSubpath()
    painter.setPen(Qt.PenStyle.NoPen);painter.setBrush(QColor(ICON_COLOR));painter.drawPath(note)
    painter.restore()
