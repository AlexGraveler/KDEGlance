// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "formatprobe.h"

#include "filetype.h"
#include "formatregistry.h"
#include "views/imagedecoder.h"
#include "views/midisupport.h"
#include "views/textview.h"
#include "views/pdfview.h"
#include "views/officeview.h"
#include <QPlainTextEdit>
#include <QProcess>
#include <QTemporaryDir>

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QTextStream>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

using namespace Qt::StringLiterals;

namespace KDEGlance::FormatProbe
{

namespace
{
ProbeResult probeMedia(ProbeResult r, const QString &path, int timeoutMs)
{
    QMediaPlayer player;
    QVideoSink sink;
    QAudioBufferOutput audioOut;
    player.setVideoSink(&sink);
    player.setAudioBufferOutput(&audioOut);

    QEventLoop loop;
    bool gotFrame = false;
    bool gotAudio = false;
    bool loaded = false;
    QString error;
    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop, [&](const QVideoFrame &f) {
        if (f.isValid() && !f.toImage().isNull()) {
            gotFrame = true;
            loop.quit();
        }
    });
    QObject::connect(&audioOut, &QAudioBufferOutput::audioBufferReceived, &loop, [&](const QAudioBuffer &b) {
        if (b.isValid() && b.frameCount() > 0) {
            gotAudio = true;
            if (!player.hasVideo()) {
                loop.quit();
            }
        }
    });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop, [&](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) {
            loaded = true;
        } else if (s == QMediaPlayer::InvalidMedia || s == QMediaPlayer::EndOfMedia) {
            loop.quit();
        }
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, [&](QMediaPlayer::Error, const QString &msg) {
        error = msg.isEmpty() ? u"error"_s : msg;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(path));
    player.play();
    loop.exec();
    const bool hasVideo = player.hasVideo();
    player.stop();
    player.setSource(QUrl());

    if (!error.isEmpty()) {
        r.ok = false;
        r.detail = error;
    } else if (hasVideo) {
        r.ok = gotFrame;
        r.kind = u"video"_s;
        r.detail = gotFrame ? u"video frame decoded"_s : (loaded ? u"loaded, but no frame"_s : u"timeout"_s);
    } else {
        r.ok = gotAudio;
        r.kind = u"audio"_s;
        r.detail = gotAudio ? u"audio decoded"_s : (loaded ? u"loaded, but no audio"_s : u"timeout"_s);
    }
    return r;
}
ProbeResult probeDocument(ProbeResult r, const QString &path, ContentKind kind, int timeoutMs)
{
    std::unique_ptr<ContentView> view;
    if(kind==ContentKind::Text)view=std::make_unique<TextView>();
    else if(kind==ContentKind::Pdf)view=std::make_unique<PdfView>();
    else view=std::make_unique<OfficeView>();
    QEventLoop loop;QString error;bool ready=false,done=false;
    QTimer poll;poll.setInterval(20);
    QObject::connect(view.get(),&ContentView::ready,&loop,[&]{ready=true;});
    QObject::connect(view.get(),&ContentView::failed,&loop,[&](const QString &message){error=message;done=true;loop.quit();});
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{
        if(!ready)return;
        PdfView *pdf=qobject_cast<PdfView *>(view.get());
        if(auto *office=qobject_cast<OfficeView *>(view.get()))pdf=office->pdfView();
        if(!pdf||pdf->cachedPages()>0){done=true;loop.quit();}
    });
    QTimer::singleShot(kind==ContentKind::Office?std::max(timeoutMs,60000):timeoutMs,&loop,&QEventLoop::quit);
    view->resize(800,700);view->show();view->load({QUrl::fromLocalFile(path),path,detectFileType(path)});
    poll.start();if(!done)loop.exec();
    r.ok=done&&error.isEmpty();
    r.detail=r.ok ? u"document displayed"_s : (error.isEmpty()?u"document render timed out"_s:error);
    view->unload();return r;
}
} // namespace

ProbeResult probeFile(const QString &path, int timeoutMs)
{
    ProbeResult r;
    const QFileInfo fi(path);
    r.file = fi.fileName();
    r.extension = fi.suffix().toLower();
    const FileTypeInfo type = detectFileType(path);
    r.kind = contentKindName(type.kind);
    if (const FormatInfo *f = FormatRegistry::forExtension(r.extension)) {
        r.tier = tierName(f->tier);
    }

    switch (type.kind) {
    case ContentKind::Image:
    case ContentKind::Svg:
    case ContentKind::Raw: {
        const ImageProbe probe = ImageDecoder::probe(path, type.kind);
        if (!probe.ok) {
            r.detail = probe.error;
            return r;
        }
        if (probe.animated) {
            QImageReader reader(path);
            const QImage a = reader.read();
            const QImage b = reader.read();
            r.ok = !a.isNull() && !b.isNull();
            r.detail = r.ok ? u"animation: %1x%2, >=2 frames"_s.arg(a.width()).arg(a.height()) : u"animation frames not decodable"_s;
            return r;
        }
        const DecodedImage img = ImageDecoder::decode(path, type.kind, QSize(512, 512), 0, true, makeCancelToken());
        r.ok = img.ok && !img.image.isNull();
        r.detail = r.ok ? u"%1x%2%3%4"_s.arg(img.naturalSize.width())
                              .arg(img.naturalSize.height())
                              .arg(probe.pageCount > 1 ? u", %1 pages"_s.arg(probe.pageCount) : QString())
                              .arg(img.fromEmbeddedPreview ? u", embedded preview"_s : QString())
                        : img.error;
        return r;
    }
    case ContentKind::Text:
    case ContentKind::Pdf:
    case ContentKind::Office: return probeDocument(r,path,type.kind,timeoutMs);
    case ContentKind::Video:
    case ContentKind::Audio:
        return probeMedia(r, path, timeoutMs);
    case ContentKind::Midi: {
        const QString synth = MidiSupport::synthesizer();
        const QString font = MidiSupport::soundfont();
        if (synth.isEmpty() || font.isEmpty()) {
            r.detail = u"MIDI unavailable: install fluidsynth and a SoundFont (soundfont-fluid)"_s;
            return r;
        }
        QTemporaryDir dir;
        if (!dir.isValid()) {
            r.detail = u"Cannot create a temporary directory for MIDI"_s;
            return r;
        }
        const QString wav = dir.filePath(u"render.wav"_s);
        QProcess render;
        render.start(synth, MidiSupport::arguments(wav, font, path));
        if (!render.waitForFinished(timeoutMs) || render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0) {
            render.kill();
            render.waitForFinished(1000);
            r.detail = u"FluidSynth could not render MIDI: "_s + QString::fromUtf8(render.readAllStandardError()).left(512);
            return r;
        }
        return probeMedia(r, wav, timeoutMs);
    }
    case ContentKind::StreamLink:
        r.ok = !type.streamUrls.isEmpty();
        r.detail = r.ok ? u"stream link: %1"_s.arg(type.streamUrls.first()) : u"no URL"_s;
        return r;
    default:
        r.detail = u"not a media file (%1)"_s.arg(type.mimeType.name());
        return r;
    }
}

QList<ProbeResult> probeDirectory(const QString &dir, int timeoutMs)
{
    QList<ProbeResult> results;
    const QStringList files = QDir(dir).entryList(QDir::Files, QDir::Name);
    for (const QString &f : files) {
        if (f == u"SKIPPED.txt"_s) {
            continue; // written by tests/formats/generate_samples.sh
        }
        results << probeFile(QDir(dir).absoluteFilePath(f), timeoutMs);
    }
    return results;
}

int runCli(const QString &dir, bool json)
{
    const QList<ProbeResult> results = probeDirectory(dir);
    int mandatoryFailures = 0;
    for (const ProbeResult &r : results) {
        if (!r.ok && r.tier == u"mandatory"_s) {
            ++mandatoryFailures;
        }
    }
    QTextStream out(stdout);
    if (json) {
        QJsonArray arr;
        for (const ProbeResult &r : results) {
            arr.append(QJsonObject{{u"file"_s, r.file},
                                   {u"extension"_s, r.extension},
                                   {u"kind"_s, r.kind},
                                   {u"tier"_s, r.tier},
                                   {u"ok"_s, r.ok},
                                   {u"detail"_s, r.detail}});
        }
        out << QJsonDocument(arr).toJson(QJsonDocument::Indented);
    } else {
        for (const ProbeResult &r : results) {
            out << (r.ok ? u"OK   "_s : u"FAIL "_s) << r.file.leftJustified(28) << u' ' << r.kind.leftJustified(8) << u' ' << r.tier.leftJustified(12)
                << u' ' << r.detail << u'\n';
        }
        out << u"\n%1 files, %2 failed, %3 mandatory failures\n"_s.arg(results.size())
                   .arg(std::count_if(results.cbegin(), results.cend(), [](const ProbeResult &r) {
                       return !r.ok;
                   }))
                   .arg(mandatoryFailures);
    }
    return mandatoryFailures == 0 ? 0 : 1;
}

} // namespace KDEGlance::FormatProbe
