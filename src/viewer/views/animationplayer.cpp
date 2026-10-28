// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "animationplayer.h"

#include "imagedecoder.h"
#include "l10n.h"

#include <QImageReader>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <deque>
#include <vector>

namespace KDEGlance
{

namespace
{
constexpr size_t QueueAhead = 6; // frames decoded ahead of display
constexpr int StarvedRetryMs = 8; // decoder slower than playback
} // namespace

struct AnimationPlayer::Shared {
    struct Frame {
        QImage image;
        int delay = 100;
    };
    QMutex mutex;
    QWaitCondition wake;
    std::deque<Frame> queue;
    std::vector<Frame> loop; // complete animation, when it fit into the budget
    bool loopReady = false;
    bool stop = false;
    QString error;
};

AnimationPlayer::AnimationPlayer(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AnimationPlayer::advance);
}

AnimationPlayer::~AnimationPlayer()
{
    stop();
}

int AnimationPlayer::effectiveDelay(int delayMs)
{
    return delayMs <= 10 ? 100 : delayMs;
}

void AnimationPlayer::start(const QString &path)
{
    stop();
    auto shared = std::make_shared<Shared>();
    m_shared = shared;
    m_gotFirst = false;
    m_index = -1;
    m_cacheIndex = 0;
    m_clock.start();
    m_nextDue = 0;

    QThread *worker = QThread::create([shared, path] {
        auto openReader = [&path] {
            auto r = std::make_unique<QImageReader>(path);
            r->setAutoTransform(true);
            r->setDecideFormatFromContent(true);
            return r;
        };
        std::unique_ptr<QImageReader> reader = openReader();
        std::vector<Shared::Frame> cache;
        qint64 cacheBytes = 0;
        bool cacheable = true;
        bool firstPass = true;
        int decodedInPass = 0;

        while (true) {
            {
                QMutexLocker lock(&shared->mutex);
                if (shared->stop) {
                    return;
                }
            }
            QImage img = reader->read();
            if (img.isNull()) {
                if (decodedInPass == 0) {
                    QMutexLocker lock(&shared->mutex);
                    shared->error = reader->errorString().isEmpty() ? tr_("The image cannot be decoded") : reader->errorString();
                    return;
                }
                if (firstPass && cacheable) {
                    QMutexLocker lock(&shared->mutex);
                    shared->loop = std::move(cache);
                    shared->loopReady = true;
                    return; // GUI thread loops from memory from now on
                }
                // Too big to keep: decode again from the start.
                firstPass = false;
                decodedInPass = 0;
                reader = openReader();
                continue;
            }
            ++decodedInPass;
            Shared::Frame frame{ImageDecoder::normalizeForDisplay(img), effectiveDelay(reader->nextImageDelay())};
            if (firstPass && cacheable) {
                cacheBytes += frame.image.sizeInBytes();
                if (cacheBytes > CacheBudget) {
                    cacheable = false;
                    cache.clear();
                    cache.shrink_to_fit();
                } else {
                    cache.push_back(frame);
                }
            }
            QMutexLocker lock(&shared->mutex);
            while (shared->queue.size() >= QueueAhead && !shared->stop) {
                shared->wake.wait(&shared->mutex);
            }
            if (shared->stop) {
                return;
            }
            shared->queue.push_back(std::move(frame));
        }
    });
    // The thread owns nothing but the shared state; it deletes itself.
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start(QThread::LowPriority);

    m_timer.start(0);
}

void AnimationPlayer::stop()
{
    m_timer.stop();
    if (m_shared) {
        QMutexLocker lock(&m_shared->mutex);
        m_shared->stop = true;
        m_shared->queue.clear();
        m_shared->wake.wakeAll();
    }
    m_shared.reset();
    m_current = QImage();
    m_index = -1;
}

bool AnimationPlayer::isActive() const
{
    return bool(m_shared);
}

QImage AnimationPlayer::currentFrame() const
{
    return m_current;
}

int AnimationPlayer::frameIndex() const
{
    return m_index;
}

void AnimationPlayer::advance()
{
    if (!m_shared) {
        return;
    }
    Shared::Frame frame;
    bool have = false;
    bool loopMode = false;
    bool loopReady = false;
    size_t loopSize = 0;
    QString error;
    {
        QMutexLocker lock(&m_shared->mutex);
        if (!m_shared->queue.empty()) {
            frame = std::move(m_shared->queue.front());
            m_shared->queue.pop_front();
            m_shared->wake.wakeAll();
            have = true;
        } else if (m_shared->loopReady && !m_shared->loop.empty()) {
            loopMode = true;
            frame = m_shared->loop[size_t(m_cacheIndex) % m_shared->loop.size()];
            m_cacheIndex = int((size_t(m_cacheIndex) + 1) % m_shared->loop.size());
            have = true;
        } else {
            error = m_shared->error;
        }
        loopReady = m_shared->loopReady;
        loopSize = m_shared->loop.size();
    }
    if (!have) {
        if (!error.isEmpty() && !m_gotFirst) {
            const QString msg = error;
            stop();
            Q_EMIT failed(msg);
            return;
        }
        if (loopReady && loopSize == 0) {
            return; // everything was shown from the queue already
        }
        m_timer.start(StarvedRetryMs);
        return;
    }
    m_current = frame.image;
    ++m_index;
    const qint64 now = m_clock.elapsed();
    if (!m_gotFirst) {
        m_gotFirst = true;
        m_nextDue = now;
        Q_EMIT firstFrame(m_current.size());
    }
    Q_EMIT frameChanged();
    if (loopMode && loopSize == 1) {
        return; // a still image in an animation container
    }

    // Drift-free schedule; if we fell far behind (e.g. machine was busy),
    // resynchronise instead of fast-forwarding.
    m_nextDue += frame.delay;
    if (m_nextDue < now - 250) {
        m_nextDue = now + frame.delay;
    }
    m_timer.start(int(std::max<qint64>(0, m_nextDue - now)));
}

} // namespace KDEGlance
