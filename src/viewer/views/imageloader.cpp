// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "imageloader.h"

#include <QMetaObject>
#include <QThread>

namespace KDEGlance
{

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
{
    // Two decoders in parallel: the current file and one prefetch. More only
    // competes for memory bandwidth and makes cancellation slower.
    m_pool.setMaxThreadCount(std::max(2, std::min(3, QThread::idealThreadCount() / 2)));
    m_pool.setExpiryTimeout(30000); // idle threads go away; no idle CPU use
}

ImageLoader::~ImageLoader()
{
    {
        QMutexLocker lock(&m_tokensMutex);
        for (const auto &weak : std::as_const(m_tokens)) {
            if (auto token = weak.lock()) {
                token->store(true);
            }
        }
    }
    m_pool.clear();
    m_pool.waitForDone();
}

void ImageLoader::setRawPreferEmbedded(bool prefer)
{
    m_rawPreferEmbedded = prefer;
}

void ImageLoader::track(const CancelToken &token)
{
    QMutexLocker lock(&m_tokensMutex);
    // Drop expired entries so the list stays tiny.
    m_tokens.erase(std::remove_if(m_tokens.begin(), m_tokens.end(), [](const auto &w) { return w.expired(); }), m_tokens.end());
    m_tokens.append(token);
}

quint64 ImageLoader::probe(const QString &path, ContentKind kind, const CancelToken &token)
{
    const quint64 id = m_nextId++;
    track(token);
    m_pool.start([this, id, path, kind, token] {
        if (isCancelled(token)) {
            return;
        }
        ImageProbe result = ImageDecoder::probe(path, kind);
        if (isCancelled(token)) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, id, result] {
                Q_EMIT probeFinished(id, result);
            },
            Qt::QueuedConnection);
    });
    return id;
}

quint64 ImageLoader::decode(const QString &path, ContentKind kind, const QSize &target, int page, const CancelToken &token)
{
    const quint64 id = m_nextId++;
    track(token);
    const bool preferEmbedded = m_rawPreferEmbedded;
    m_pool.start([this, id, path, kind, target, page, token, preferEmbedded] {
        if (isCancelled(token)) {
            return;
        }
        DecodedImage result = ImageDecoder::decode(path, kind, target, page, preferEmbedded, token);
        if (isCancelled(token)) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, id, result] {
                Q_EMIT decodeFinished(id, result);
            },
            Qt::QueuedConnection);
    });
    return id;
}

void ImageLoader::waitForIdle()
{
    m_pool.waitForDone();
}

} // namespace KDEGlance
