// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "formatregistry.h"
#include "views/midisupport.h"
#include <QFileInfo>

#include "config-kdeglance.h"
#include "l10n.h"

#include <QImageReader>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
using T = FormatInfo::Tier;
using B = FormatInfo::Backend;
using K = ContentKind;

const QString kimg = u"kimageformats"_s;
const QString qtimg = u"qt6-imageformats"_s;

QList<FormatInfo> buildList()
{
    QList<FormatInfo> l;
    auto img = [&l](QString id, QString name, QStringList ext, T tier, QString key, QString req) {
        l.append({id, name, ext, K::Image, tier, B::QtImage, key, req});
    };
    // --- still and animated images ------------------------------------------------
    img(u"jpeg"_s, u"JPEG"_s, {u"jpg"_s, u"jpeg"_s, u"jpe"_s, u"jfif"_s}, T::Mandatory, u"jpeg"_s, u"qt6-base"_s);
    img(u"png"_s, u"PNG / APNG"_s, {u"png"_s, u"apng"_s}, T::Mandatory, u"png"_s, u"qt6-base"_s);
    img(u"gif"_s, u"GIF (animated)"_s, {u"gif"_s}, T::Mandatory, u"gif"_s, u"qt6-base"_s);
    img(u"bmp"_s, u"BMP / DIB"_s, {u"bmp"_s, u"dib"_s}, T::Mandatory, u"bmp"_s, u"qt6-base"_s);
    img(u"tiff"_s, u"TIFF (multi-page)"_s, {u"tif"_s, u"tiff"_s}, T::Mandatory, u"tiff"_s, qtimg);
    img(u"webp"_s, u"WebP (animated)"_s, {u"webp"_s}, T::Mandatory, u"webp"_s, qtimg);
    img(u"ico"_s, u"ICO"_s, {u"ico"_s, u"cur"_s}, T::Mandatory, u"ico"_s, u"qt6-base"_s);
    img(u"icns"_s, u"ICNS"_s, {u"icns"_s}, T::Mandatory, u"icns"_s, qtimg);
    img(u"tga"_s, u"TGA"_s, {u"tga"_s}, T::Mandatory, u"tga"_s, qtimg);
    img(u"pnm"_s, u"PNM / PBM / PGM / PPM"_s, {u"pnm"_s, u"pbm"_s, u"pgm"_s, u"ppm"_s}, T::Mandatory, u"ppm"_s, u"qt6-base"_s);
    img(u"pcx"_s, u"PCX"_s, {u"pcx"_s}, T::Mandatory, u"pcx"_s, kimg);
    img(u"psd"_s, u"PSD / PSB (composite)"_s, {u"psd"_s, u"psb"_s}, T::Mandatory, u"psd"_s, kimg);
    img(u"avif"_s, u"AVIF"_s, {u"avif"_s, u"avifs"_s}, T::Conditional, u"avif"_s, kimg + u" + libavif"_s);
    img(u"heif"_s, u"HEIF / HEIC"_s, {u"heif"_s, u"heic"_s, u"hif"_s}, T::Conditional, u"heif"_s, kimg + u" + libheif"_s);
    img(u"jxl"_s, u"JPEG XL"_s, {u"jxl"_s}, T::Conditional, u"jxl"_s, kimg + u" + libjxl"_s);
    img(u"jp2"_s, u"JPEG 2000"_s, {u"jp2"_s, u"j2k"_s, u"jpf"_s, u"jpx"_s, u"jpc"_s}, T::Conditional, u"jp2"_s, kimg + u" + openjpeg2"_s);
    img(u"exr"_s, u"OpenEXR"_s, {u"exr"_s}, T::Conditional, u"exr"_s, kimg + u" + openexr"_s);
    img(u"hdr"_s, u"Radiance HDR"_s, {u"hdr"_s, u"pic"_s}, T::Mandatory, u"hdr"_s, kimg);
    img(u"qoi"_s, u"QOI"_s, {u"qoi"_s}, T::Experimental, u"qoi"_s, kimg);
    img(u"xcf"_s, u"GIMP XCF"_s, {u"xcf"_s}, T::Experimental, u"xcf"_s, kimg);
    img(u"kra"_s, u"Krita / OpenRaster"_s, {u"kra"_s, u"ora"_s}, T::Experimental, u"kra"_s, kimg);

    l.append({u"svg"_s, u"SVG / SVGZ"_s, {u"svg"_s, u"svgz"_s}, K::Svg, T::Mandatory, B::QtSvg, u"svg"_s, u"qt6-svg"_s});

    // --- camera RAW (LibRaw) -------------------------------------------------------
    auto raw = [&l](QString id, QString name, QStringList ext, T tier) {
        l.append({id, name, ext, K::Raw, tier, B::LibRaw, QString(), u"libraw"_s});
    };
    raw(u"raw-canon"_s, u"Canon CR2 / CR3 / CRW"_s, {u"cr2"_s, u"cr3"_s, u"crw"_s}, T::Mandatory);
    raw(u"raw-nikon"_s, u"Nikon NEF / NRW"_s, {u"nef"_s, u"nrw"_s}, T::Mandatory);
    raw(u"raw-sony"_s, u"Sony ARW / SRF / SR2"_s, {u"arw"_s, u"srf"_s, u"sr2"_s}, T::Mandatory);
    raw(u"raw-fuji"_s, u"Fujifilm RAF"_s, {u"raf"_s}, T::Mandatory);
    raw(u"raw-olympus"_s, u"Olympus / OM System ORF"_s, {u"orf"_s}, T::Mandatory);
    raw(u"raw-panasonic"_s, u"Panasonic / Leica RW2 / RWL"_s, {u"rw2"_s, u"rwl"_s, u"raw"_s}, T::Mandatory);
    raw(u"raw-pentax"_s, u"Pentax PEF"_s, {u"pef"_s, u"ptx"_s}, T::Mandatory);
    raw(u"raw-dng"_s, u"Adobe DNG"_s, {u"dng"_s}, T::Mandatory);
    raw(u"raw-samsung"_s, u"Samsung SRW"_s, {u"srw"_s}, T::Mandatory);
    raw(u"raw-hasselblad"_s, u"Hasselblad 3FR / FFF"_s, {u"3fr"_s, u"fff"_s}, T::Conditional);
    raw(u"raw-phaseone"_s, u"Phase One IIQ / EIP"_s, {u"iiq"_s, u"eip"_s}, T::Conditional);
    raw(u"raw-sigma"_s, u"Sigma X3F"_s, {u"x3f"_s}, T::Experimental);
    raw(u"raw-epson"_s, u"Epson ERF"_s, {u"erf"_s}, T::Conditional);
    raw(u"raw-kodak"_s, u"Kodak DCR / KDC"_s, {u"dcr"_s, u"kdc"_s, u"dcs"_s}, T::Conditional);
    raw(u"raw-other"_s, u"Minolta MRW, Leaf MOS, Mamiya MEF"_s, {u"mrw"_s, u"mos"_s, u"mef"_s}, T::Conditional);

    // --- video ------------------------------------------------------------------------
    auto media = [&l](QString id, QString name, QStringList ext, K kind, T tier) {
        l.append({id, name, ext, kind, tier, B::Media, QString(), u"ffmpeg + qt6-multimedia-ffmpeg"_s});
    };
    media(u"mp4"_s, u"MP4 / M4V"_s, {u"mp4"_s, u"m4v"_s}, K::Video, T::Mandatory);
    media(u"mkv"_s, u"Matroska MKV"_s, {u"mkv"_s}, K::Video, T::Mandatory);
    media(u"webm"_s, u"WebM"_s, {u"webm"_s}, K::Video, T::Mandatory);
    media(u"mov"_s, u"QuickTime MOV / QT"_s, {u"mov"_s, u"qt"_s}, K::Video, T::Mandatory);
    media(u"avi"_s, u"AVI / DivX / Xvid"_s, {u"avi"_s, u"divx"_s}, K::Video, T::Mandatory);
    media(u"wmv"_s, u"WMV / ASF"_s, {u"wmv"_s, u"asf"_s}, K::Video, T::Mandatory);
    media(u"mpeg"_s, u"MPEG-1/2 (MPG, MPE, VCD/SVCD)"_s, {u"mpeg"_s, u"mpg"_s, u"mpe"_s, u"m1v"_s, u"m2v"_s, u"dat"_s}, K::Video, T::Mandatory);
    media(u"ts"_s, u"MPEG-TS (TS, MTS, M2TS)"_s, {u"ts"_s, u"mts"_s, u"m2ts"_s}, K::Video, T::Mandatory);
    media(u"vob"_s, u"DVD VOB"_s, {u"vob"_s}, K::Video, T::Mandatory);
    media(u"flv"_s, u"Flash FLV / F4V"_s, {u"flv"_s, u"f4v"_s}, K::Video, T::Mandatory);
    media(u"3gp"_s, u"3GP / 3G2"_s, {u"3gp"_s, u"3g2"_s}, K::Video, T::Mandatory);
    media(u"ogv"_s, u"Ogg Video"_s, {u"ogv"_s, u"ogm"_s}, K::Video, T::Mandatory);
    media(u"mxf"_s, u"MXF"_s, {u"mxf"_s}, K::Video, T::Conditional);
    media(u"dv"_s, u"DV"_s, {u"dv"_s}, K::Video, T::Mandatory);
    media(u"rm"_s, u"RealMedia RM / RMVB"_s, {u"rm"_s, u"rmvb"_s}, K::Video, T::Conditional);

    // --- audio ------------------------------------------------------------------------
    media(u"mp3"_s, u"MPEG audio MP3 / MP2 / MP1"_s, {u"mp3"_s, u"mp2"_s, u"mp1"_s, u"mpa"_s}, K::Audio, T::Mandatory);
    media(u"flac"_s, u"FLAC"_s, {u"flac"_s}, K::Audio, T::Mandatory);
    media(u"wav"_s, u"WAV"_s, {u"wav"_s, u"wave"_s}, K::Audio, T::Mandatory);
    media(u"aiff"_s, u"AIFF / AIFC"_s, {u"aiff"_s, u"aif"_s, u"aifc"_s}, K::Audio, T::Mandatory);
    media(u"aac"_s, u"AAC / M4A / M4B / ALAC"_s, {u"aac"_s, u"m4a"_s, u"m4b"_s, u"alac"_s}, K::Audio, T::Mandatory);
    media(u"ogg"_s, u"Ogg Vorbis"_s, {u"ogg"_s, u"oga"_s}, K::Audio, T::Mandatory);
    media(u"opus"_s, u"Opus"_s, {u"opus"_s}, K::Audio, T::Mandatory);
    media(u"wma"_s, u"WMA"_s, {u"wma"_s}, K::Audio, T::Mandatory);
    media(u"ape"_s, u"Monkey's Audio APE"_s, {u"ape"_s}, K::Audio, T::Mandatory);
    media(u"wv"_s, u"WavPack"_s, {u"wv"_s}, K::Audio, T::Mandatory);
    media(u"mpc"_s, u"Musepack"_s, {u"mpc"_s}, K::Audio, T::Mandatory);
    media(u"tak"_s, u"TAK"_s, {u"tak"_s}, K::Audio, T::Mandatory);
    media(u"tta"_s, u"TTA"_s, {u"tta"_s}, K::Audio, T::Mandatory);
    media(u"shn"_s, u"Shorten"_s, {u"shn"_s}, K::Audio, T::Mandatory);
    media(u"ac3"_s, u"AC-3 / E-AC-3"_s, {u"ac3"_s, u"eac3"_s, u"ec3"_s}, K::Audio, T::Mandatory);
    media(u"dts"_s, u"DTS"_s, {u"dts"_s}, K::Audio, T::Mandatory);
    media(u"amr"_s, u"AMR"_s, {u"amr"_s}, K::Audio, T::Conditional);
    media(u"au"_s, u"AU / SND"_s, {u"au"_s, u"snd"_s}, K::Audio, T::Mandatory);
    media(u"ra"_s, u"RealAudio RA / RMA"_s, {u"ra"_s, u"rma"_s}, K::Audio, T::Conditional);
    media(u"tracker"_s, u"Tracker MOD / XM / S3M / IT"_s, {u"mod"_s, u"xm"_s, u"s3m"_s, u"it"_s, u"mptm"_s}, K::Audio, T::Conditional);
    l.append({u"midi"_s, u"MIDI"_s, {u"mid"_s, u"midi"_s, u"kar"_s, u"rmi"_s}, K::Midi, T::Experimental, B::Midi, QString(), u"fluidsynth + soundfont-fluid"_s});
    l.append({u"ram"_s, u"RealMedia metafile RAM"_s, {u"ram"_s}, K::StreamLink, T::Experimental, B::StreamLink, QString(), QString()});
    l.append({u"text"_s,u"Text"_s,{u"txt"_s,u"log"_s,u"md"_s,u"csv"_s,u"ini"_s,u"json"_s,u"xml"_s,u"yaml"_s,u"yml"_s,u"conf"_s,u"toml"_s},K::Text,T::Mandatory,B::Document,QString(),u"qt6-5compat"_s});
    l.append({u"pdf"_s,u"PDF"_s,{u"pdf"_s},K::Pdf,T::Mandatory,B::Document,QString(),u"poppler-qt6"_s});
    l.append({u"office"_s,u"Office documents"_s,{u"doc"_s,u"docx"_s,u"docm"_s,u"rtf"_s,u"odt"_s,u"ott"_s,u"fodt"_s},K::Office,T::Mandatory,B::Document,QString(),u"libreoffice-fresh or libreoffice-still"_s});
    l.append({u"pages"_s,u"Apple Pages"_s,{u"pages"_s},K::Office,T::Experimental,B::Document,QString(),u"LibreOffice import filter"_s});
    return l;
}
} // namespace

QString tierName(FormatInfo::Tier tier)
{
    switch (tier) {
    case FormatInfo::Tier::Mandatory:
        return u"mandatory"_s;
    case FormatInfo::Tier::Conditional:
        return u"conditional"_s;
    case FormatInfo::Tier::Experimental:
        return u"experimental"_s;
    }
    return QString();
}

const QList<FormatInfo> &FormatRegistry::all()
{
    static const QList<FormatInfo> list = buildList();
    return list;
}

const FormatInfo *FormatRegistry::forExtension(const QString &extension)
{
    const QString e = extension.toLower();
    for (const FormatInfo &f : all()) {
        if (f.extensions.contains(e)) {
            return &f;
        }
    }
    return nullptr;
}

FormatRegistry::Availability FormatRegistry::availability(const FormatInfo &format)
{
    switch (format.backend) {
    case FormatInfo::Backend::QtImage: {
        static const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        return formats.contains(format.pluginKey.toLatin1()) ? Availability::Available : Availability::Missing;
    }
    case FormatInfo::Backend::QtSvg: {
#if HAVE_QTSVG
        return Availability::Available;
#else
        return Availability::Missing;
#endif
    }
    case FormatInfo::Backend::LibRaw:
#if HAVE_LIBRAW
        return Availability::Available;
#else
        return QImageReader::supportedImageFormats().contains("raw") ? Availability::Available : Availability::Missing;
#endif
    case FormatInfo::Backend::Document:
        if(format.kind==ContentKind::Office) return QFileInfo(QStringLiteral("/usr/bin/libreoffice")).isExecutable()?Availability::Available:Availability::Missing;
        return Availability::Available;
    case FormatInfo::Backend::Midi:
        return MidiSupport::synthesizer().isEmpty() || MidiSupport::soundfont().isEmpty() ? Availability::Missing : Availability::Available;
    case FormatInfo::Backend::Media:
    case FormatInfo::Backend::StreamLink:
        return Availability::Unknown;
    }
    return Availability::Unknown;
}

QString FormatRegistry::missingDecoderHint(const QString &extension)
{
    const FormatInfo *f = forExtension(extension);
    if (!f || f->requirement.isEmpty()) {
        return QString();
    }
    return tr_("Required: %1").arg(f->requirement);
}

} // namespace KDEGlance
