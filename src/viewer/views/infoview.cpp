// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "infoview.h"

#include "l10n.h"
#include "style.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

InfoView::InfoView(QWidget *parent)
    : ContentView(parent)
    , m_icon(new QLabel(this))
    , m_title(new QLabel(this))
    , m_details(new QLabel(this))
    , m_message(new QLabel(this))
    , m_play(new QPushButton(tr_("Play stream"), this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(10);
    layout->addStretch(1);
    layout->addWidget(m_icon, 0, Qt::AlignHCenter);
    layout->addWidget(m_title);
    layout->addWidget(m_details);
    layout->addWidget(m_message);
    layout->addWidget(m_play, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    QFont tf = m_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    m_title->setFont(tf);
    for (QLabel *l : {m_title, m_details, m_message}) {
        l->setAlignment(Qt::AlignHCenter);
        l->setWordWrap(true);
        l->setTextInteractionFlags(Qt::NoTextInteraction);
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_details->setForegroundRole(QPalette::PlaceholderText);
    m_play->setFocusPolicy(Qt::NoFocus);
    m_play->hide();
    connect(m_play, &QPushButton::clicked, this, [this] {
        Q_EMIT playStreamRequested(m_streamUrl, m_title->text());
    });
}

QSize InfoView::naturalSize() const
{
    return QSize(440, 320);
}

void InfoView::fill(const PreviewItem &item)
{
    const QFileInfo fi(item.path);
    QIcon icon;
    if (item.type.mimeType.isValid()) {
        icon = QIcon::fromTheme(item.type.mimeType.iconName(), QIcon::fromTheme(item.type.mimeType.genericIconName()));
    }
    if (icon.isNull()) {
        icon = QIcon::fromTheme(item.type.kind == ContentKind::Directory ? u"folder"_s : u"unknown"_s);
    }
    m_icon->setPixmap(icon.pixmap(96, 96));
    m_icon->setVisible(!icon.isNull());
    m_title->setText(fi.fileName().isEmpty() ? item.url.toDisplayString() : fi.fileName());

    QStringList details;
    if (item.type.mimeType.isValid() && !item.type.mimeType.comment().isEmpty()) {
        details << item.type.mimeType.comment();
    }
    const QLocale locale;
    if (item.type.kind == ContentKind::Directory) {
        const int count = int(QDir(item.path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
        details << tr_("%1 items").arg(count);
    } else if (fi.exists()) {
        details << locale.formattedDataSize(fi.size());
    }
    if (fi.exists()) {
        details << locale.toString(fi.lastModified(), QLocale::ShortFormat);
    }
    m_details->setText(details.join(u"  ·  "_s));
}

void InfoView::load(const PreviewItem &item)
{
    unload();
    fill(item);
    if (item.type.kind == ContentKind::StreamLink && !item.type.streamUrls.isEmpty()) {
        m_streamUrl = QUrl(item.type.streamUrls.first());
        QStringList lines;
        for (const QString &u : item.type.streamUrls) {
            lines << u;
        }
        m_message->setText(tr_("RealMedia link to a stream:") + u"\n"_s + lines.join(u'\n'));
        const QString scheme = m_streamUrl.scheme().toLower();
        // FFmpeg plays http(s), rtsp and mms; "pnm" is obsolete.
        m_play->setVisible(scheme == u"http"_s || scheme == u"https"_s || scheme.startsWith(u"rtsp"_s) || scheme.startsWith(u"mms"_s)
                           || scheme == u"file"_s);
    } else if (item.type.kind == ContentKind::Other || item.type.kind == ContentKind::Directory) {
        m_message->setText(QString());
    }
    QTimer::singleShot(0, this, &InfoView::ready);
}

void InfoView::showLoading()
{
    unload();
    m_loading = true;
    update();
}

void InfoView::showError(const PreviewItem &item, const QString &message)
{
    unload();
    fill(item);
    if (!QFileInfo::exists(item.path) && !item.path.isEmpty()) {
        m_icon->setPixmap(QPixmap());
        m_details->setText(QString());
    }
    m_message->setText(message);
    update();
}

void InfoView::unload()
{
    m_loading = false;
    m_streamUrl = QUrl();
    m_play->hide();
    m_message->clear();
    m_details->clear();
    m_title->clear();
    m_icon->clear();
}

QString InfoView::headline() const
{
    return m_title->text();
}

QString InfoView::message() const
{
    return m_message->text();
}

void InfoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    if (m_loading) { p.fillRect(rect(), Qt::black); p.setPen(Qt::white); p.drawText(rect(), Qt::AlignCenter, tr_("Loading…")); return; }
    p.setClipPath(Style::windowShapeFor(this));
    p.fillRect(rect(), Style::windowBackground(this));
}

void InfoView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragCandidate = true;
        m_pressPos = event->position();
        event->accept();
        return;
    }
    event->ignore();
}

void InfoView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragCandidate && (event->buttons() & Qt::LeftButton)
        && (event->position() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragCandidate = false;
        requestWindowMove();
        event->accept();
        return;
    }
    event->ignore();
}

} // namespace KDEGlance
