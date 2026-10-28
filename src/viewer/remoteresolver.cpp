#include "l10n.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "remoteresolver.h"
#include <KIO/CopyJob>
#include <KIO/FileCopyJob>
#include <KJob>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>

namespace KDEGlance {
RemoteResolver::RemoteResolver(QObject* parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(120000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        cancel();
        Q_EMIT failed(tr_("Remote file loading timed out"));
    });
}
RemoteResolver::~RemoteResolver() { cancel(); }
void RemoteResolver::cancel()
{
    m_timeout.stop();
    ++m_generation;
    if (m_job) {
        m_job->disconnect(this);
        m_job->kill(KJob::Quietly);
    }
    m_job = nullptr;
    m_cache.reset();
}
void RemoteResolver::resolve(const QUrl& url)
{
    cancel();
    const auto generation = m_generation;
    if (url.isLocalFile()) {
        Q_EMIT resolved(url.toLocalFile());
        return;
    }
    if (!url.isValid() || url.scheme().isEmpty()) {
        Q_EMIT failed(tr_("Invalid remote URL"));
        return;
    }
    m_timeout.start();
    // KIO Fuse provides random access and avoids downloading a whole movie.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KIOFuse"), QStringLiteral("/org/kde/KIOFuse"),
        QStringLiteral("org.kde.KIOFuse.VFS"), QStringLiteral("mountUrl"));
    message << url.toString();
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 15000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, url, generation](QDBusPendingCallWatcher* w) {
        const QDBusPendingReply<QString> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (!reply.isError() && !reply.value().isEmpty()) {
            m_timeout.stop();
            Q_EMIT resolved(reply.value());
        } else
            copyFallback(url, generation);
    });
}
void RemoteResolver::copyFallback(const QUrl& url, quint64 generation)
{
    m_cache = std::make_unique<QTemporaryDir>();
    if (!m_cache->isValid()) {
        m_timeout.stop();
        Q_EMIT failed(tr_("Cannot create a temporary cache for the remote file"));
        return;
    }
    const QString extension = QFileInfo(url.path()).suffix().left(16);
    const QString path = m_cache->filePath(QStringLiteral("preview.") + extension);
    auto* job = KIO::file_copy(url, QUrl::fromLocalFile(path), -1, KIO::HideProgressInfo);
    m_job = job;
    const auto checkLimit = [this, generation](KJob*, KJob::Unit unit, qulonglong amount) {
        if (generation == m_generation && unit == KJob::Bytes && amount > 2ull * 1024 * 1024 * 1024) {
            cancel();
            Q_EMIT failed(tr_("Remote file exceeds the 2 GiB download cache limit; enable KIO Fuse for streaming"));
        }
    };
    connect(job, &KJob::totalAmountChanged, this, checkLimit);
    connect(job, &KJob::processedAmountChanged, this, checkLimit);
    connect(job, &KJob::result, this, [this, job, path, generation] {
        if (generation != m_generation)
            return;
        m_job = nullptr;
        m_timeout.stop();
        if (job->error()) {
            m_cache.reset();
            Q_EMIT failed(tr_("Cannot read the remote file: %1").arg(job->errorString()));
        } else
            Q_EMIT resolved(path);
    });
}
}
