// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "filetype.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMimeDatabase>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
// Extension → kind for formats that shared-mime-info does not describe (or
// describes ambiguously). Only consulted when content detection is generic.
const QHash<QString, ContentKind> &extensionFallback()
{
    static const QHash<QString, ContentKind> table = [] {
        QHash<QString, ContentKind> t;
        for (const QString &e : rawExtensions()) {
            t.insert(e, ContentKind::Raw);
        }
        for (const auto &e : {u"txt"_s,u"log"_s,u"md"_s,u"markdown"_s,u"csv"_s,u"ini"_s,u"json"_s,u"xml"_s,u"yaml"_s,u"yml"_s,u"conf"_s,u"toml"_s,u"cpp"_s,u"h"_s,u"py"_s,u"js"_s,u"sh"_s,u"sql"_s,u"css"_s}) t.insert(e, ContentKind::Text);
        for (const auto &e : {u"doc"_s,u"docx"_s,u"docm"_s,u"rtf"_s,u"odt"_s,u"ott"_s,u"fodt"_s,u"pages"_s}) t.insert(e, ContentKind::Office);
        t.insert(u"pdf"_s, ContentKind::Pdf);
        const auto images = {u"hdr"_s, u"psb"_s, u"jfif"_s, u"dib"_s, u"pbm"_s, u"pgm"_s, u"ppm"_s, u"pnm"_s, u"j2k"_s, u"jpf"_s, u"jpx"_s, u"jxl"_s, u"qoi"_s};
        for (const auto &e : images) {
            t.insert(e, ContentKind::Image);
        }
        const auto audio = {u"rma"_s, u"eac3"_s, u"ec3"_s, u"mp1"_s, u"mp2"_s, u"aifc"_s, u"tak"_s, u"tta"_s, u"shn"_s, u"snd"_s,
                            u"mod"_s, u"xm"_s,   u"s3m"_s, u"it"_s,  u"mptm"_s, u"669"_s,  u"mtm"_s, u"umx"_s, u"alac"_s, u"caf"_s};
        for (const auto &e : audio) {
            t.insert(e, ContentKind::Audio);
        }
        const auto video = {u"rm"_s,  u"rmvb"_s, u"divx"_s, u"vob"_s, u"mts"_s, u"m2ts"_s, u"mxf"_s, u"dv"_s,
                            u"f4v"_s, u"3g2"_s,  u"qt"_s,   u"mpe"_s, u"m1v"_s, u"m2v"_s,  u"dat"_s, u"ogm"_s};
        for (const auto &e : video) {
            t.insert(e, ContentKind::Video);
        }
        t.insert(u"mid"_s, ContentKind::Midi);
        t.insert(u"midi"_s, ContentKind::Midi);
        t.insert(u"kar"_s, ContentKind::Midi);
        t.insert(u"rmi"_s, ContentKind::Midi);
        t.insert(u"ram"_s, ContentKind::StreamLink);
        return t;
    }();
    return table;
}

bool isGeneric(const QMimeType &mime)
{
    return !mime.isValid() || mime.isDefault() || mime.name() == u"text/plain"_s || mime.name() == u"application/x-zerosize"_s;
}

bool inheritsAny(const QMimeType &mime, std::initializer_list<QStringView> names)
{
    for (const auto &n : names) {
        if (mime.inherits(n.toString())) {
            return true;
        }
    }
    return false;
}

ContentKind kindForMime(const QMimeType &mime)
{
    if (!mime.isValid()) {
        return ContentKind::Other;
    }
    const QString name = mime.name();
    if (name == u"application/pdf"_s) return ContentKind::Pdf;
    if (inheritsAny(mime, {u"application/msword", u"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
                          u"application/vnd.ms-word.document.macroEnabled.12", u"application/vnd.oasis.opendocument.text",
                          u"application/vnd.oasis.opendocument.text-template", u"application/rtf", u"text/rtf"})) return ContentKind::Office;
    if (name.startsWith(u"text/"_s) || name == u"application/json"_s || name == u"application/xml"_s || name == u"application/yaml"_s) return ContentKind::Text;
    if (mime.inherits(u"image/x-dcraw"_s)) {
        return ContentKind::Raw;
    }
    if (name == u"image/svg+xml"_s || name == u"image/svg+xml-compressed"_s) {
        return ContentKind::Svg;
    }
    if (inheritsAny(mime, {u"application/ram", u"audio/x-pn-realaudio-plugin"})) {
        return ContentKind::StreamLink;
    }
    if (inheritsAny(mime, {u"audio/midi", u"audio/x-midi"})) {
        return ContentKind::Midi;
    }
    if (name.startsWith(u"image/"_s)) {
        return ContentKind::Image;
    }
    if (name.startsWith(u"video/"_s)
        || inheritsAny(mime,
                       {u"application/vnd.rn-realmedia", u"application/vnd.rn-realmedia-vbr", u"application/vnd.ms-asf", u"application/mxf",
                        u"application/x-matroska", u"application/x-flash-video", u"application/ogg"})) {
        return ContentKind::Video;
    }
    if (name.startsWith(u"audio/"_s) || inheritsAny(mime, {u"application/x-shorten"})) {
        return ContentKind::Audio;
    }
    if (name == u"inode/directory"_s) {
        return ContentKind::Directory;
    }
    return ContentKind::Other;
}
} // namespace

const QStringList &rawExtensions()
{
    static const QStringList list{
        u"cr2"_s, u"cr3"_s, u"crw"_s, // Canon
        u"nef"_s, u"nrw"_s, // Nikon
        u"arw"_s, u"srf"_s, u"sr2"_s, // Sony
        u"raf"_s, // Fujifilm
        u"orf"_s, // Olympus / OM System
        u"rw2"_s, u"rwl"_s, u"raw"_s, // Panasonic / Leica
        u"pef"_s, u"ptx"_s, // Pentax
        u"dng"_s, // Adobe / universal
        u"srw"_s, // Samsung
        u"3fr"_s, u"fff"_s, // Hasselblad
        u"iiq"_s, u"eip"_s, // Phase One
        u"x3f"_s, // Sigma
        u"erf"_s, // Epson
        u"dcr"_s, u"kdc"_s, u"dcs"_s, // Kodak
        u"mrw"_s, // Minolta
        u"mos"_s, // Leaf
        u"mef"_s, // Mamiya
        u"rwz"_s, u"bay"_s, u"cap"_s,
    };
    return list;
}

QString contentKindName(ContentKind kind)
{
    switch (kind) {
    case ContentKind::Missing:
        return u"missing"_s;
    case ContentKind::Directory:
        return u"directory"_s;
    case ContentKind::Image:
        return u"image"_s;
    case ContentKind::Svg:
        return u"svg"_s;
    case ContentKind::Raw:
        return u"raw"_s;
    case ContentKind::Video:
        return u"video"_s;
    case ContentKind::Audio:
        return u"audio"_s;
    case ContentKind::Midi:
        return u"midi"_s;
    case ContentKind::StreamLink:
        return u"streamlink"_s;
    case ContentKind::Text: return u"text"_s;
    case ContentKind::Pdf: return u"pdf"_s;
    case ContentKind::Office: return u"office"_s;
    case ContentKind::Other:
        return u"other"_s;
    }
    return u"other"_s;
}

SizeCategory sizeCategoryFor(ContentKind kind)
{
    switch (kind) {
    case ContentKind::Text: return SizeCategory::Text;
    case ContentKind::Pdf: return SizeCategory::Pdf;
    case ContentKind::Office: return SizeCategory::Office;
    case ContentKind::Video:
        return SizeCategory::Video;
    case ContentKind::Audio:
    case ContentKind::Midi:
    case ContentKind::StreamLink:
        return SizeCategory::Audio;
    default:
        return SizeCategory::Image;
    }
}

QStringList parseStreamLinkFile(const QByteArray &data)
{
    // Binary RealMedia starts with ".RMF"; metafiles are short text files.
    if (data.isEmpty() || data.size() > 64 * 1024 || data.startsWith(".RMF") || data.contains('\0')) {
        return {};
    }
    QStringList urls;
    static const QRegularExpression scheme(u"^(rtsp|rtspu|rtspt|pnm|http|https|mms|mmsh|file|rtmp)://\\S+$"_s, QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(data);
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        // Optional "?title=..." parameters are part of the URL and kept.
        if (scheme.match(line).hasMatch()) {
            urls << line;
        }
    }
    return urls;
}

FileTypeInfo detectFileType(const QString &localPath)
{
    FileTypeInfo info;
    const QFileInfo fi(localPath);
    if (!fi.exists()) {
        info.kind = ContentKind::Missing;
        return info;
    }
    const QMimeDatabase db;
    if (fi.isDir()) {
        info.kind = ContentKind::Directory;
        info.mimeType = db.mimeTypeForName(u"inode/directory"_s);
        return info;
    }
    if (!fi.isReadable()) {
        info.kind = ContentKind::Missing;
        return info;
    }

    const QMimeType byContent = db.mimeTypeForFile(fi, QMimeDatabase::MatchContent);
    const QMimeType byName = db.mimeTypeForFile(fi, QMimeDatabase::MatchExtension);
    info.mimeByContent = byContent.name();
    info.mimeByName = byName.name();

    QMimeType effective;
    if (isGeneric(byContent)) {
        effective = byName;
    } else if (byName.isValid() && !byName.isDefault() && byName.inherits(byContent.name())) {
        // The name is a refinement of what the content says (NEF is a TIFF,
        // M4B is MP4, ...): keep the more specific type.
        effective = byName;
    } else {
        effective = byContent;
        info.extensionMismatch = !byName.isDefault() && byName.isValid() && !byContent.inherits(byName.name()) && byName != byContent;
    }
    info.mimeType = effective;
    info.kind = kindForMime(effective);

    const QString suffix = fi.suffix().toLower();
    const ContentKind byExtension = extensionFallback().value(suffix, ContentKind::Other);

    // RAW containers are TIFF/ISO-BMFF/proprietary by content; the extension
    // decides as long as the content does not clearly say "audio/video/text".
    if (byExtension == ContentKind::Raw) {
        const bool contradicts = byContent.name().startsWith(u"audio/"_s) || byContent.name().startsWith(u"text/"_s)
            || (byContent.name().startsWith(u"video/"_s) && suffix != u"cr3"_s);
        if (!contradicts) {
            info.kind = ContentKind::Raw;
        }
    } else if ((info.kind == ContentKind::Other || (info.kind == ContentKind::Text && isGeneric(byContent))) && byExtension != ContentKind::Other) {
        info.kind = byExtension;
    }

    if (info.kind == ContentKind::Other && byContent.name() == u"text/plain"_s) info.kind = ContentKind::Text;
    // Office ZIP/XML containers need their extension to refine generic data.
    if (byExtension == ContentKind::Office && (effective.name().contains(u"zip"_s) || effective.name().contains(u"xml"_s) || info.kind == ContentKind::Text)) info.kind = ContentKind::Office;
    // .ram may be a text metafile or (rarely) real RealMedia data.
    if (info.kind == ContentKind::StreamLink || suffix == u"ram"_s || suffix == u"rpm"_s) {
        QFile f(localPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray head = f.read(64 * 1024 + 1);
            info.streamUrls = parseStreamLinkFile(head);
            if (!info.streamUrls.isEmpty()) {
                info.kind = ContentKind::StreamLink;
            } else if (head.startsWith(".RMF")) {
                info.kind = ContentKind::Video;
            } else if (info.kind == ContentKind::StreamLink) {
                info.kind = ContentKind::Other;
            }
        }
    }
    return info;
}

FileTypeInfo detectUrlType(const QUrl &url)
{
    if (url.isLocalFile()) return detectFileType(url.toLocalFile());
    FileTypeInfo info;
    info.mimeType = QMimeDatabase().mimeTypeForFile(url.path(), QMimeDatabase::MatchExtension);
    info.kind = kindForMime(info.mimeType);
    const auto fallback = extensionFallback().value(QFileInfo(url.path()).suffix().toLower(), ContentKind::Other);
    if (fallback != ContentKind::Other && (info.kind == ContentKind::Other || info.kind == ContentKind::Text)) info.kind = fallback;
    return info;
}

} // namespace KDEGlance
