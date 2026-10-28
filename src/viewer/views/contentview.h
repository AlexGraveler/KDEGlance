// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "filetype.h"
#include "settings.h"

#include <QAbstractButton>
#include <QPointer>
#include <QUrl>
#include <QWidget>

namespace KDEGlance
{

struct PreviewItem {
    QUrl url;
    QString path; // local path
    FileTypeInfo type;
};

/*
 * Base of the widgets that render one kind of content inside the preview
 * window (image, media, information card). The window owns one instance of
 * each and reuses it for every file of that kind, so switching between
 * files does not recreate widgets, decoders or audio streams.
 */
class ContentView : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual void load(const PreviewItem &item) = 0;
    // Stop playback, cancel work and drop decoded data.
    virtual void unload() = 0;
    virtual void resetPresentation() {}

    virtual SizeCategory sizeCategory() const = 0;
    // Natural content size in logical pixels; invalid if not known yet.
    virtual QSize naturalSize() const
    {
        return QSize();
    }
    // Lets the view use keys that are not reserved by the window.
    virtual bool handleKey(QKeyEvent *event)
    {
        Q_UNUSED(event)
        return false;
    }
    // Neighbouring files the view may decode in advance.
    virtual void prefetch(const QList<PreviewItem> &items)
    {
        Q_UNUSED(items)
    }

Q_SIGNALS:
    // The content can be shown (first image decoded, media loaded) or failed.
    void systemMoveRequested(); // Observability of the shared compositor request.
    void navigateRequested(int delta);
    void closeRequested();
    void ready();
    void failed(const QString &message);
    void naturalSizeChanged(const QSize &size);
    void sizeCategoryChanged(KDEGlance::SizeCategory category);

protected:
    // Starts a compositor-driven window move (smooth on Wayland and X11).
    void requestWindowMove();
    void enableDocumentDragging(QWidget* viewport);
    bool eventFilter(QObject* watched, QEvent* event) override;
    void contentClick(const QPointF &position, const QRectF &visible);

private:
    QPointer<QWidget> m_documentDrag;
    QPointF m_documentPress;

};

// Round translucent button used on top of images and media.
class OverlayButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit OverlayButton(int glyph, QWidget *parent = nullptr);
    void setGlyph(int glyph);
    void setBackdrop(bool enabled);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_glyph;
    bool m_hover = false;
    bool m_backdrop = true;
};

} // namespace KDEGlance
