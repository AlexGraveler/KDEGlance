// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "imagedecoder.h"

namespace KDEGlance::RawDecoder
{
bool isAvailable();

// Reads only the metadata (fast).
ImageProbe probe(const QString &path);

/*
 * Fast path first: the embedded JPEG/bitmap preview, if it is large enough
 * for targetSize (or the caller does not prefer it). Otherwise a real
 * demosaic: half size when that still covers targetSize, full size when
 * targetSize is invalid. The result is oriented and in sRGB.
 */
DecodedImage decode(const QString &path, const QSize &targetSize, bool preferEmbedded, const CancelToken &token);
}
