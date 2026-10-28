// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include "filetype.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace KDEGlance
{

/*
 * Catalogue of the formats KDEGlance aims to preview (spec 9.4).
 *
 * Tier:
 *  Mandatory    - must work with the dependencies of the Arch package.
 *  Conditional  - works when an optional decoder/plugin is installed.
 *  Experimental - best effort (unusual codecs, synthesizers, network links).
 *
 * The registry never claims that a format works: availability() only says
 * whether the decoder that would handle it is present. Whether files really
 * decode is verified by `kdeglance --probe-formats` and the format tests,
 * which decode real sample files.
 */
struct FormatInfo {
    enum class Tier {
        Mandatory,
        Conditional,
        Experimental,
    };
    enum class Backend {
        QtImage, // QImageReader + Qt/KDE image plugins (key = plugin format name)
        LibRaw,
        QtSvg,
        Media, // Qt Multimedia (FFmpeg)
        Midi, // FluidSynth renderer
        StreamLink,
        Document,
    };

    QString id; // short id, e.g. "heif"
    QString name; // human readable
    QStringList extensions;
    ContentKind kind;
    Tier tier;
    Backend backend;
    QString pluginKey; // QImageReader format key for QtImage
    QString requirement; // what to install if it is missing
};

QString tierName(FormatInfo::Tier tier);

class FormatRegistry
{
public:
    static const QList<FormatInfo> &all();
    static const FormatInfo *forExtension(const QString &extension);

    enum class Availability {
        Available, // the decoder is installed
        Missing, // known to be missing
        Unknown, // cannot tell without decoding (media)
    };
    static Availability availability(const FormatInfo &format);

    // Human readable hint for an error message when `extension` failed to open.
    static QString missingDecoderHint(const QString &extension);
};

} // namespace KDEGlance
