// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "contentview.h"

#include "style.h"

#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QWindow>

namespace KDEGlance
{

void ContentView::enableDocumentDragging(QWidget* viewport)
{
    viewport->setProperty("kdeglanceDocumentDrag", true);
    viewport->installEventFilter(this);
}
bool ContentView::eventFilter(QObject* watched, QEvent* event)
{
    auto* viewport=qobject_cast<QWidget*>(watched);
    if (!viewport || !viewport->property("kdeglanceDocumentDrag").toBool()) return QWidget::eventFilter(watched,event);
    if (event->type()==QEvent::MouseButtonPress) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        // Shift-drag retains text selection; a plain/Alt drag moves the window.
        if (mouse->button()==Qt::LeftButton && !(mouse->modifiers() & (Qt::ShiftModifier|Qt::ControlModifier))) {
            m_documentDrag=viewport; m_documentPress=mouse->position(); return true;
        }
    } else if (event->type()==QEvent::MouseMove && m_documentDrag==viewport) {
        auto* mouse=static_cast<QMouseEvent*>(event);
        if (!(mouse->buttons() & Qt::LeftButton)) m_documentDrag=nullptr;
        else {
            if ((mouse->position()-m_documentPress).manhattanLength()>=QApplication::startDragDistance()) {
                m_documentDrag=nullptr; requestWindowMove();
            }
            return true;
        }
    } else if (event->type()==QEvent::MouseButtonRelease && m_documentDrag==viewport) {
        m_documentDrag=nullptr; return true;
    } else if (event->type()==QEvent::Hide) m_documentDrag=nullptr;
    return QWidget::eventFilter(watched,event);
}

void ContentView::contentClick(const QPointF &position, const QRectF &visible)
{
    if (visible.isEmpty() || !visible.contains(position)) return;
    const qreal fraction = (position.x() - visible.left()) / visible.width();
    if (fraction < 0.2) Q_EMIT navigateRequested(-1);
    else if (fraction >= 0.8) Q_EMIT navigateRequested(1);
    else Q_EMIT closeRequested();
}

void ContentView::requestWindowMove()
{
    Q_EMIT systemMoveRequested();
    if (QWindow *w = window()->windowHandle()) {
        if (!window()->isFullScreen()) {
            w->startSystemMove();
        }
    }
}

OverlayButton::OverlayButton(int glyph, QWidget *parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
{
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
}

void OverlayButton::setGlyph(int glyph)
{
    if (m_glyph != glyph) {
        m_glyph = glyph;
        update();
    }
}

void OverlayButton::setBackdrop(bool enabled)
{
    m_backdrop = enabled;
    update();
}

QSize OverlayButton::sizeHint() const
{
    return QSize(30, 30);
}

void OverlayButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (m_backdrop) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, m_hover ? 170 : 120));
        p.drawRoundedRect(r, 6, 6);
    } else if (m_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawEllipse(r);
    }
    const qreal inset = r.width() * 0.2;
    QColor fg(255, 255, 255, isEnabled() ? 240 : 110);
    if (isCheckable() && !isChecked()) {
        fg.setAlpha(150);
    }
    Style::paintGlyph(p, static_cast<Style::Glyph>(m_glyph), r.adjusted(inset, inset, -inset, -inset), fg);
}

void OverlayButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void OverlayButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

} // namespace KDEGlance
