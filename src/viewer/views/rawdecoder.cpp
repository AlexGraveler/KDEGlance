// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "rawdecoder.h"

#include "config-kdeglance.h"
#include "l10n.h"

#include <QBuffer>
#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QTransform>

#if HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

#include <memory>

using namespace Qt::StringLiterals;

namespace KDEGlance::RawDecoder
{

#if HAVE_LIBRAW
namespace
{
struct ProgressContext {
    CancelToken token;
};

int progressCallback(void *data, enum LibRaw_progress, int, int)
{
    auto *ctx = static_cast<ProgressContext *>(data);
    // Non-zero cancels the running LibRaw operation.
    return isCancelled(ctx->token) ? 1 : 0;
}

// LibRaw flip codes: 0 none, 3 = 180°, 5 = 90° CCW, 6 = 90° CW.
QImage applyFlip(const QImage &image, int flip)
{
    QTransform t;
    switch (flip) {
    case 3:
        t.rotate(180);
        break;
    case 5:
        t.rotate(-90);
        break;
    case 6:
        t.rotate(90);
        break;
    default:
        return image;
    }
    return image.transformed(t);
}

QSize orientedSize(int width, int height, int flip)
{
    return (flip == 5 || flip == 6) ? QSize(height, width) : QSize(width, height);
}

QImage imageFromProcessed(const libraw_processed_image_t *p, bool *hadOwnOrientation)
{
    *hadOwnOrientation = false;
    if (!p) {
        return QImage();
    }
    if (p->type == LIBRAW_IMAGE_JPEG) {
        QByteArray bytes(reinterpret_cast<const char *>(p->data), int(p->data_size));
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "jpeg");
        // The embedded preview rarely has its own orientation tag; when it
        // does, it is authoritative and the RAW flip must not be applied.
        reader.setAutoTransform(false);
        const QImageIOHandler::Transformations tr = reader.transformation();
        QImage img = reader.read();
        if (!img.isNull() && tr != QImageIOHandler::TransformationNone) {
            *hadOwnOrientation = true;
            // Re-read with auto transform to apply the tag.
            buffer.seek(0);
            QImageReader again(&buffer, "jpeg");
            again.setAutoTransform(true);
            img = again.read();
        }
        return img;
    }
    if (p->type == LIBRAW_IMAGE_BITMAP && p->bits == 8 && (p->colors == 3 || p->colors == 1)) {
        const QImage::Format fmt = p->colors == 3 ? QImage::Format_RGB888 : QImage::Format_Grayscale8;
        const int stride = p->width * p->colors;
        QImage view(p->data, p->width, p->height, stride, fmt);
        return view.copy(); // detach from LibRaw's buffer
    }
    if (p->type == LIBRAW_IMAGE_BITMAP && p->bits == 16 && p->colors == 3) {
        QImage img(p->width, p->height, QImage::Format_RGB888);
        const auto *src = reinterpret_cast<const quint16 *>(p->data);
        for (int y = 0; y < p->height; ++y) {
            uchar *dst = img.scanLine(y);
            for (int x = 0; x < p->width * 3; ++x) {
                dst[x] = uchar(src[size_t(y) * p->width * 3 + x] >> 8);
            }
        }
        return img;
    }
    return QImage();
}

struct ProcessedDeleter {
    void operator()(libraw_processed_image_t *p) const
    {
        LibRaw::dcraw_clear_mem(p);
    }
};
using ProcessedPtr = std::unique_ptr<libraw_processed_image_t, ProcessedDeleter>;
} // namespace
#endif

bool isAvailable()
{
    return HAVE_LIBRAW;
}

ImageProbe probe(const QString &path)
{
    ImageProbe result;
    result.format = "raw";
#if HAVE_LIBRAW
    auto raw = std::make_unique<LibRaw>();
    const QByteArray encoded = QFile::encodeName(path);
    const int rc = raw->open_file(encoded.constData());
    if (rc != LIBRAW_SUCCESS) {
        result.error = tr_("Unsupported or damaged RAW file (%1)").arg(QString::fromLatin1(libraw_strerror(rc)));
        return result;
    }
    const auto &s = raw->imgdata.sizes;
    result.naturalSize = orientedSize(s.width, s.height, s.flip);
    result.ok = result.naturalSize.isValid();
    if (!result.ok) {
        result.error = tr_("Unsupported or damaged RAW file (%1)").arg(u"size"_s);
    }
#else
    Q_UNUSED(path)
    result.error = tr_("RAW support is not available (KDEGlance was built without LibRaw)");
#endif
    return result;
}

DecodedImage decode(const QString &path, const QSize &targetSize, bool preferEmbedded, const CancelToken &token)
{
    DecodedImage result;
#if HAVE_LIBRAW
    auto raw = std::make_unique<LibRaw>();
    ProgressContext ctx{token};
    raw->set_progress_handler(progressCallback, &ctx);

    const QByteArray encoded = QFile::encodeName(path);
    int rc = raw->open_file(encoded.constData());
    if (rc != LIBRAW_SUCCESS) {
        result.error = tr_("Unsupported or damaged RAW file (%1)").arg(QString::fromLatin1(libraw_strerror(rc)));
        return result;
    }
    const int flip = raw->imgdata.sizes.flip;
    result.naturalSize = orientedSize(raw->imgdata.sizes.width, raw->imgdata.sizes.height, flip);
    const int rawLongSide = std::max(result.naturalSize.width(), result.naturalSize.height());
    const int targetLongSide = targetSize.isValid() ? std::max(targetSize.width(), targetSize.height()) : rawLongSide;

    // 1. Embedded preview (typically a full-size or 1-2 MP JPEG): fastest.
    if (preferEmbedded && !isCancelled(token)) {
        if (raw->unpack_thumb() == LIBRAW_SUCCESS) {
            const int thumbLong = std::max<int>(raw->imgdata.thumbnail.twidth, raw->imgdata.thumbnail.theight);
            // Good enough if it covers the display size (or is at least half
            // of the sensor resolution, when the full image is requested).
            const bool enough = thumbLong >= std::min(targetLongSide, rawLongSide / 2) || thumbLong >= 2048;
            if (enough) {
                int err = 0;
                ProcessedPtr thumb(raw->dcraw_make_mem_thumb(&err));
                bool ownOrientation = false;
                QImage img = imageFromProcessed(thumb.get(), &ownOrientation);
                if (!img.isNull()) {
                    if (!ownOrientation) {
                        img = applyFlip(img, flip);
                    }
                    result.ok = true;
                    result.fromEmbeddedPreview = true;
                    result.image = ImageDecoder::normalizeForDisplay(img);
                    result.isFullResolution = std::max(img.width(), img.height()) >= rawLongSide;
                    return result;
                }
            }
        }
    }
    if (isCancelled(token)) {
        return result;
    }

    // 2. Demosaic. Half size is 4x faster and enough for a fitted preview.
    const bool halfSize = targetSize.isValid() && targetLongSide * 2 <= rawLongSide;
    raw->imgdata.params.half_size = halfSize ? 1 : 0;
    raw->imgdata.params.use_camera_wb = 1;
    raw->imgdata.params.output_bps = 8;
    raw->imgdata.params.output_color = 1; // sRGB
    raw->imgdata.params.no_auto_bright = 0;

    rc = raw->unpack();
    if (rc != LIBRAW_SUCCESS) {
        result.error = isCancelled(token) ? QString() : tr_("Cannot decode RAW data (%1)").arg(QString::fromLatin1(libraw_strerror(rc)));
        return result;
    }
    rc = raw->dcraw_process();
    if (rc != LIBRAW_SUCCESS) {
        result.error = isCancelled(token) ? QString() : tr_("Cannot decode RAW data (%1)").arg(QString::fromLatin1(libraw_strerror(rc)));
        return result;
    }
    int err = 0;
    ProcessedPtr processed(raw->dcraw_make_mem_image(&err)); // already oriented by LibRaw
    bool unused = false;
    QImage img = imageFromProcessed(processed.get(), &unused);
    if (img.isNull()) {
        result.error = tr_("Cannot decode RAW data (%1)").arg(QString::fromLatin1(libraw_strerror(err)));
        return result;
    }
    img.setColorSpace(QColorSpace::SRgb);
    result.ok = true;
    result.isFullResolution = !halfSize;
    result.image = ImageDecoder::normalizeForDisplay(img);
#else
    Q_UNUSED(path)
    Q_UNUSED(targetSize)
    Q_UNUSED(preferEmbedded)
    Q_UNUSED(token)
    result.error = tr_("RAW support is not available (KDEGlance was built without LibRaw)");
#endif
    return result;
}

} // namespace KDEGlance::RawDecoder
