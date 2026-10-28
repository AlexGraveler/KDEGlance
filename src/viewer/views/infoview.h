// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "contentview.h"

class QLabel;
class QPushButton;

namespace KDEGlance
{

/*
 * Information card: used for files without a visual preview (documents,
 * archives, folders), for RealMedia metafiles (stream links) and to explain
 * errors (missing decoder, damaged or deleted file).
 */
class InfoView : public ContentView
{
    Q_OBJECT
public:
    explicit InfoView(QWidget *parent = nullptr);

    void load(const PreviewItem &item) override;
    void unload() override;
    SizeCategory sizeCategory() const override
    {
        return SizeCategory::Image;
    }
    QSize naturalSize() const override;

    void showLoading();
    void showError(const PreviewItem &item, const QString &message);

    QString headline() const;
    QString message() const;

Q_SIGNALS:
    void playStreamRequested(const QUrl &url, const QString &title);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void fill(const PreviewItem &item);

    QLabel *m_icon;
    QLabel *m_title;
    QLabel *m_details;
    QLabel *m_message;
    QPushButton *m_play;
    QUrl m_streamUrl;
    QPointF m_pressPos;
    bool m_loading = false;
    bool m_dragCandidate = false;
};

} // namespace KDEGlance
