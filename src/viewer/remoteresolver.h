// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
class KJob;
namespace KDEGlance {
class RemoteResolver : public QObject {
    Q_OBJECT
public:
    explicit RemoteResolver(QObject* parent = nullptr);
    ~RemoteResolver() override;
    void resolve(const QUrl& url);
    void cancel();
Q_SIGNALS:
    void resolved(const QString& localPath);
    void failed(const QString& message);

private:
    void copyFallback(const QUrl& url, quint64 generation);
    QTimer m_timeout;
    quint64 m_generation = 0;
    QPointer<KJob> m_job;
    std::unique_ptr<QTemporaryDir> m_cache;
};
}
