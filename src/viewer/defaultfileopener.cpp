// SPDX-License-Identifier: GPL-3.0-or-later
#include "defaultfileopener.h"
#include "constants.h"
#include "l10n.h"
#include <KApplicationTrader>
#include <KIO/JobUiDelegate>
#include <KIO/OpenUrlJob>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>

namespace KDEGlance {
namespace {
    // Retain KDE's Open With / executable handling, but report errors asynchronously
    // ourselves, without a nested modal event loop in the resident viewer service.
    class OpenUiDelegate : public KIO::JobUiDelegate {
    public:
        OpenUiDelegate()
            : KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, nullptr)
        {
        }
    };
}
void DefaultFileOpener::open(const QUrl& url)
{
    if (m_pending.contains(url))
        return;
    if (!url.isValid() || url.isEmpty()) {
        Q_EMIT failed(url, tr_("Cannot open an empty or invalid URL"));
        return;
    }
    m_pending.insert(url);
    auto* job = new KIO::OpenUrlJob(url, this);
    job->setUiDelegate(new OpenUiDelegate);
    job->setShowOpenOrExecuteDialog(true);
    connect(job, &KIO::OpenUrlJob::mimeTypeFound, this, [this, job, url](const QString& mime) {
        const auto service = KApplicationTrader::preferredService(mime);
        if (!service)
            return; // KIO presents the standard Open With dialog.
        const auto command = QProcess::splitCommand(service->exec());
        const bool self = service->desktopEntryName() == QString(AppId)
            || (!command.isEmpty() && QFileInfo(command.first()).fileName() == QStringLiteral("kdeglance"));
        if (self) {
            job->kill(KJob::Quietly);
            m_pending.remove(url);
            Q_EMIT failed(
                url, tr_("KDEGlance is the default application for this file. Choose another application in Dolphin."));
        }
    });
    connect(job, &KJob::result, this, [this, url](KJob* result) {
        m_pending.remove(url);
        if (result->error())
            Q_EMIT failed(url, result->errorString());
        else
            Q_EMIT finished(url);
    });
    // Keep the service responsive even if a remote server never answers.
    QTimer::singleShot(120000, job, [job] { job->kill(KJob::EmitResult); });
    job->start();
}
}
