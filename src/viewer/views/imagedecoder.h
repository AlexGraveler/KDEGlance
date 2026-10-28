// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "filetype.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <memory>

namespace KDEGlance
{

// Shared cancellation flag of one decode request.
using CancelToken = std::shared_ptr<std::atomic_bool>;
inline CancelToken makeCancelToken()
{
    return std::make_shared<std::atomic_bool>(false);
}
inline bool isCancelled(const CancelToken &token)
{
    return token && token->load(std::memory_order_relaxed);
}

struct ImageProbe {
    bool ok = false;
    QString error;
    QSize naturalSize; // as displayed (EXIF/RAW orientation applied)
    bool animated = false;
    int pageCount = 1; // >1 for multi-page TIFF and similar
    QByteArray format; // QImageReader format key, "raw" or "svg"
};

struct DecodedImage {
    bool ok = false;
    QString error;
    QImage image; // sRGB, orientation applied, possibly downscaled for display
    QImage fullImage; // full resolution if it was decoded anyway and is not huge
    QSize naturalSize; // full resolution size as displayed
    bool isFullResolution = false; // image == full resolution
    bool fromEmbeddedPreview = false; // RAW: embedded JPEG used
    int page = 0;
};

/*
 * Synchronous decoding helpers. They run in worker threads (ImageLoader);
 * each checks the token between expensive steps and returns early when it
 * is set.
 */
namespace ImageDecoder
{
ImageProbe probe(const QString &path, ContentKind kind);

// targetSize: device pixels the image will be shown at (fit size). An
// invalid size means "full resolution".
DecodedImage decode(const QString &path, ContentKind kind, const QSize &targetSize, int page, bool rawPreferEmbedded, const CancelToken &token);

// Converts to sRGB (if tagged otherwise) and to a format that paints fast.
QImage normalizeForDisplay(QImage image);

// Multi-image containers that are size variants rather than pages.
bool isIconContainer(const QByteArray &format);
}

} // namespace KDEGlance
