// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "imagedecoder.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QThreadPool>

namespace KDEGlance
{

/*
 * Runs ImageDecoder work in a small private thread pool and reports results
 * on the GUI thread. Every request carries a CancelToken; callers cancel
 * stale requests (fast file switching) and simply ignore late results by id.
 * The destructor cancels everything and waits, so no worker outlives the
 * loader.
 */
class ImageLoader : public QObject
{
    Q_OBJECT
public:
    explicit ImageLoader(QObject *parent = nullptr);
    ~ImageLoader() override;

    void setRawPreferEmbedded(bool prefer);

    quint64 probe(const QString &path, ContentKind kind, const CancelToken &token);
    quint64 decode(const QString &path, ContentKind kind, const QSize &target, int page, const CancelToken &token);

    // Waits until all running jobs are done (tests).
    void waitForIdle();

Q_SIGNALS:
    void probeFinished(quint64 id, const KDEGlance::ImageProbe &probe);
    void decodeFinished(quint64 id, const KDEGlance::DecodedImage &image);

private:
    void track(const CancelToken &token);

    QThreadPool m_pool;
    quint64 m_nextId = 1;
    bool m_rawPreferEmbedded = true;
    QMutex m_tokensMutex;
    QList<std::weak_ptr<std::atomic_bool>> m_tokens;
};

} // namespace KDEGlance
