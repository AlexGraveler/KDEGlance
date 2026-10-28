// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "imagedecoder.h"

#include "config-kdeglance.h"
#include "l10n.h"
#include "rawdecoder.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>

#if HAVE_QTSVG
#include <QSvgRenderer>
#endif

using namespace Qt::StringLiterals;

namespace KDEGlance::ImageDecoder
{

namespace
{
// Keep a decoded full-resolution image next to the display copy only below
// this size; bigger ones are decoded again on demand (zoom).
constexpr qint64 KeepFullBytes = 256ll * 1024 * 1024;

bool rotates90(QImageIOHandler::Transformations t)
{
    return t & QImageIOHandler::TransformationRotate90;
}

QString readerError(const QImageReader &reader)
{
    switch (reader.error()) {
    case QImageReader::UnsupportedFormatError:
        return tr_("No decoder for this image format is installed");
    case QImageReader::InvalidDataError:
        return tr_("The image data is damaged or incomplete");
    case QImageReader::FileNotFoundError:
        return tr_("The file is not available");
    case QImageReader::DeviceError:
        return tr_("The file cannot be read");
    default:
        break;
    }
    const QString msg = reader.errorString();
    return msg.isEmpty() ? tr_("The image cannot be decoded") : msg;
}

#if HAVE_QTSVG
QSize svgNaturalSize(const QSvgRenderer &renderer)
{
    QSize s = renderer.defaultSize();
    if (s.isEmpty()) {
        const QRectF box = renderer.viewBoxF();
        s = box.size().toSize();
    }
    if (s.isEmpty()) {
        s = QSize(512, 512);
    }
    return s;
}
#endif
} // namespace

bool isIconContainer(const QByteArray &format)
{
    return format == "ico" || format == "icns" || format == "cur";
}

QImage normalizeForDisplay(QImage image)
{
    if (image.isNull()) {
        return image;
    }
    const QColorSpace cs = image.colorSpace();
    if (cs.isValid() && cs != QColorSpace(QColorSpace::SRgb)) {
        // Tone/colour conversion of wide-gamut, HDR or ICC-tagged images to
        // the sRGB space the screen is assumed to use.
        image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    // 8-bit premultiplied ARGB / RGB32 paint fastest with the raster engine.
    const QImage::Format target = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
    if (image.format() != target) {
        image.convertTo(target);
    }
    return image;
}

ImageProbe probe(const QString &path, ContentKind kind)
{
    if (kind == ContentKind::Raw) {
        ImageProbe p = RawDecoder::probe(path);
        if (p.ok || RawDecoder::isAvailable()) {
            return p;
        }
        // No LibRaw: try a Qt image plugin (kimageformats "raw") below.
    }
    ImageProbe result;
    if (kind == ContentKind::Svg) {
#if HAVE_QTSVG
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            result.error = tr_("The SVG document is invalid");
            return result;
        }
        result.ok = true;
        result.format = "svg";
        result.naturalSize = svgNaturalSize(renderer);
        result.animated = renderer.animated();
        return result;
#endif
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    // TGA has no magic signature: content-only detection can mistake its
    // header for ICO. Keep content-first detection for other formats (e.g.
    // BMP files named .dib), using the extension for TGA only.
    reader.setDecideFormatFromContent(QFileInfo(path).suffix().compare(u"tga"_s, Qt::CaseInsensitive) != 0);
    if (!reader.canRead()) {
        result.error = readerError(reader);
        return result;
    }
    result.format = reader.format();
    QSize size = reader.size();
    if (rotates90(reader.transformation())) {
        size.transpose();
    }
    result.naturalSize = size;
    // Some handlers (TIFF) report the count reliably only on a fresh reader.
    QImageReader counter(path);
    counter.setDecideFormatFromContent(QFileInfo(path).suffix().compare(u"tga"_s, Qt::CaseInsensitive) != 0);

    const int count = counter.imageCount();
    result.animated = reader.supportsAnimation() && count != 1;
    if (!result.animated && count > 1 && !isIconContainer(result.format)) {
        result.pageCount = count;
    }
    result.ok = true; // a missing size is resolved when decoding
    return result;
}

DecodedImage decode(const QString &path, ContentKind kind, const QSize &targetSize, int page, bool rawPreferEmbedded, const CancelToken &token)
{
    DecodedImage result;
    result.page = page;

    if (kind == ContentKind::Raw && RawDecoder::isAvailable()) {
        return RawDecoder::decode(path, targetSize, rawPreferEmbedded, token);
    }

    if (kind == ContentKind::Svg) {
#if HAVE_QTSVG
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) {
            result.error = tr_("The SVG document is invalid");
            return result;
        }
        const QSize natural = svgNaturalSize(renderer);
        result.naturalSize = natural;
        QSize renderSize = targetSize.isValid() ? natural.scaled(targetSize, Qt::KeepAspectRatio) : natural;
        // Vector data: render at the requested size, capped to a sane maximum.
        renderSize = renderSize.boundedTo(QSize(8192, 8192));
        QImage img(renderSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        renderer.render(&painter, QRectF(QPointF(0, 0), renderSize));
        painter.end();
        result.ok = true;
        result.image = img;
        result.isFullResolution = true; // re-rendered on zoom instead
        return result;
#else
        result.error = tr_("SVG support is not available (Qt SVG is missing)");
        return result;
#endif
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    // TGA has no magic signature: content-only detection can mistake its
    // header for ICO. Keep content-first detection for other formats (e.g.
    // BMP files named .dib), using the extension for TGA only.
    reader.setDecideFormatFromContent(QFileInfo(path).suffix().compare(u"tga"_s, Qt::CaseInsensitive) != 0);
    if (!reader.canRead()) {
        result.error = readerError(reader);
        return result;
    }
    const QByteArray format = reader.format();

    if (isIconContainer(format)) {
        // Pick the largest variant of an icon file.
        int best = 0;
        QSize bestSize;
        for (int i = 0; i < reader.imageCount(); ++i) {
            if (!reader.jumpToImage(i)) {
                break;
            }
            const QSize s = reader.size();
            if (s.width() * s.height() > bestSize.width() * bestSize.height()) {
                bestSize = s;
                best = i;
            }
        }
        reader.jumpToImage(best);
    } else if (page > 0) {
        if (!reader.jumpToImage(page)) {
            result.error = tr_("Page %1 is not available").arg(page + 1);
            return result;
        }
    }

    const QImageIOHandler::Transformations transformation = reader.transformation();
    QSize stored = reader.size();
    QSize natural = stored;
    if (rotates90(transformation)) {
        natural.transpose();
    }

    bool scaledDecode = false;
    if (targetSize.isValid() && stored.isValid() && reader.supportsOption(QImageIOHandler::ScaledSize)) {
        // ScaledSize works in stored orientation (before EXIF rotation).
        QSize targetStored = targetSize;
        if (rotates90(transformation)) {
            targetStored.transpose();
        }
        if (stored.width() > targetStored.width() * 3 / 2 || stored.height() > targetStored.height() * 3 / 2) {
            reader.setScaledSize(stored.scaled(targetStored, Qt::KeepAspectRatio));
            scaledDecode = true;
        }
    }
    if (isCancelled(token)) {
        return result;
    }

    QImage img = reader.read();
    if (img.isNull()) {
        result.error = readerError(reader);
        return result;
    }
    if (isCancelled(token)) {
        return result;
    }
    if (!natural.isValid()) {
        natural = img.size();
    }
    result.naturalSize = natural;
    img = normalizeForDisplay(img);

    if (scaledDecode) {
        result.image = img;
        result.isFullResolution = false;
    } else if (targetSize.isValid() && (img.width() > targetSize.width() * 3 / 2 || img.height() > targetSize.height() * 3 / 2)) {
        result.image = img.scaled(img.size().scaled(targetSize, Qt::KeepAspectRatio), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        result.isFullResolution = false;
        if (img.sizeInBytes() <= KeepFullBytes) {
            result.fullImage = img;
        }
    } else {
        result.image = img;
        result.isFullResolution = true;
    }
    result.ok = true;
    return result;
}

} // namespace KDEGlance::ImageDecoder
