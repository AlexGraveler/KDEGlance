// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>

namespace KDEGlance
{

/*
 * Plays animated images (GIF, APNG, animated WebP/AVIF/JXL, ...).
 *
 * Frames are decoded by a worker thread a few frames ahead; if the whole
 * animation fits into a memory budget it is decoded only once and then looped
 * from memory. The GUI thread only swaps frames on a precise timer whose
 * schedule is corrected for drift, so playback speed matches the file.
 * Frame delays of 10 ms or less are shown as 100 ms, as browsers do.
 *
 * stop() (or destruction) ends playback immediately and releases all frames;
 * the worker exits on its own without blocking the GUI.
 */
class AnimationPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AnimationPlayer(QObject *parent = nullptr);
    ~AnimationPlayer() override;

    void start(const QString &path);
    void stop();
    bool isActive() const;

    QImage currentFrame() const;
    int frameIndex() const; // index within the loop (for tests)

    static int effectiveDelay(int delayMs);

    // Memory budget for keeping all frames (bytes).
    static constexpr qint64 CacheBudget = 192ll * 1024 * 1024;

Q_SIGNALS:
    void frameChanged();
    void firstFrame(const QSize &size);
    void failed(const QString &message);

private:
    void advance();

    struct Shared;
    std::shared_ptr<Shared> m_shared;
    QTimer m_timer;
    QImage m_current;
    int m_index = -1;
    int m_cacheIndex = 0;
    bool m_gotFirst = false;
    qint64 m_nextDue = 0;
    QElapsedTimer m_clock;
};

} // namespace KDEGlance
