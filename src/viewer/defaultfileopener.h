// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QUrl>
namespace KDEGlance {
// Delegates MIME discovery, remote access and application choice to KIO.
class DefaultFileOpener : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void open(const QUrl& url);
Q_SIGNALS:
    void finished(const QUrl& url);
    void failed(const QUrl& url, const QString& message);

private:
    QSet<QUrl> m_pending;
};
}
