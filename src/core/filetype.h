// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "settings.h"

#include <QMimeType>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace KDEGlance
{

enum class ContentKind {
    Missing, // file does not exist or is not readable
    Directory,
    Image, // still or animated raster image handled by QImageReader
    Svg,
    Raw, // digital camera RAW
    Video, // audio/video container; may turn out to be audio-only
    Audio, // audio container; may turn out to contain video
    Midi, // needs a synthesizer
    StreamLink, // RealMedia metafile (.ram) or similar playlist pointing to streams
    Text,
    Pdf,
    Office,
    Other, // shown as an information card
};

QString contentKindName(ContentKind kind);

struct FileTypeInfo {
    ContentKind kind = ContentKind::Missing;
    QMimeType mimeType; // effective type
    QString mimeByContent; // what the bytes say
    QString mimeByName; // what the file name says
    bool extensionMismatch = false; // name and content disagree (informational)
    QStringList streamUrls; // for StreamLink
};

/*
 * Determines what a file really is. The content (magic bytes) wins over the
 * extension; the extension is used to refine a generic content match (e.g. a
 * NEF is TIFF by content but RAW by name) and as a fallback for formats that
 * have no magic. Never blocks for long: reads at most a few KiB.
 */
FileTypeInfo detectFileType(const QString &localPath);
FileTypeInfo detectUrlType(const QUrl &url);

// Which window-size group a kind belongs to (spec 5.4).
SizeCategory sizeCategoryFor(ContentKind kind);

// Extensions of camera RAW formats recognised even when shared-mime-info
// does not know them.
const QStringList &rawExtensions();

// Parses a RealMedia metafile / simple stream playlist: returns the stream
// URLs it contains, or an empty list if the data is not such a text file.
QStringList parseStreamLinkFile(const QByteArray &data);

} // namespace KDEGlance
