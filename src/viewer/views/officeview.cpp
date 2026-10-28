#include "l10n.h"
// SPDX-License-Identifier: GPL-3.0-or-later
#include "officeview.h"
#include "pdfview.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <signal.h>

namespace KDEGlance {
OfficeView::OfficeView(QWidget* parent)
    : ContentView(parent)
    , m_pdf(new PdfView(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* loading = new QLabel(tr_("Loading document…"), this);
    loading->setAlignment(Qt::AlignCenter);
    layout->addWidget(loading);
    layout->addWidget(m_pdf);
    m_pdf->hide();
    connect(m_pdf, &ContentView::ready, this, [this, loading] {
        loading->hide();
        m_pdf->show();
        Q_EMIT ready();
    });
    connect(m_pdf, &ContentView::failed, this, &ContentView::failed);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(120000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        unload();
        Q_EMIT failed(tr_("Office conversion timed out"));
    });
}
void OfficeView::resetPresentation() { m_pdf->resetPresentation(); }
OfficeView::~OfficeView() { unload(); }
void OfficeView::stopConverter()
{
    if (!m_process)
        return;
    auto* process = m_process.data();
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        // A dedicated session/process group never includes the user's office session.
        const auto pid = process->processId();
        if (pid > 0)
            ::kill(-pid, SIGKILL);
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}
void OfficeView::unload()
{
    m_timeout.stop();
    stopConverter();
    m_pdf->unload();
    m_pdf->hide();
    m_cache.reset();
    m_log.clear();
    for (auto* label : findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly))
        label->show();
}
bool OfficeView::handleKey(QKeyEvent* e) { return m_pdf->handleKey(e); }
void OfficeView::load(const PreviewItem& item)
{
    unload();
    // Prefer the real system executable over wrappers or user-defined shell aliases.
    QString office = QStringLiteral("/usr/bin/libreoffice");
    if (!QFileInfo(office).isExecutable())
        office = QStandardPaths::findExecutable(QStringLiteral("libreoffice"));
    if (office.isEmpty()) {
        Q_EMIT failed(tr_("Office preview needs LibreOffice (libreoffice-fresh or libreoffice-still)"));
        return;
    }
    m_cache = std::make_unique<QTemporaryDir>();
    if (!m_cache->isValid()) {
        Q_EMIT failed(tr_("Cannot create a temporary office profile"));
        return;
    }
    const QString profile = m_cache->filePath(QStringLiteral("profile"));
    QDir().mkpath(profile + QStringLiteral("/user"));
    QDir().mkpath(m_cache->filePath(QStringLiteral("pdf")));
    QFile configuration(profile + QStringLiteral("/user/registrymodifications.xcu"));
    if (!configuration.open(QIODevice::WriteOnly)) {
        unload();
        Q_EMIT failed(tr_("Cannot secure the temporary office profile"));
        return;
    }
    const QByteArray settings = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry">
<item oor:path="/org.openoffice.Office.Common/Security/Scripting">
<prop oor:name="DisableMacrosExecution" oor:op="fuse" oor:finalized="true"><value>true</value></prop>
<prop oor:name="MacroSecurityLevel" oor:op="fuse" oor:finalized="true"><value>3</value></prop>
</item>
<item oor:path="/org.openoffice.Office.Writer/Content/Update"><prop oor:name="Link" oor:op="fuse" oor:finalized="true"><value>2</value></prop></item>
<item oor:path="/org.openoffice.Office.Calc/Content/Update"><prop oor:name="Link" oor:op="fuse" oor:finalized="true"><value>2</value></prop></item>
</oor:items>)XML";
    if (configuration.write(settings) != settings.size()) {
        configuration.close();
        unload();
        Q_EMIT failed(tr_("Cannot secure the temporary office profile"));
        return;
    }
    configuration.close();
    auto* process = new QProcess(this);
    m_process = process;
    QProcess::UnixProcessParameters parameters;
    parameters.flags = QProcess::UnixProcessFlag::CreateNewSession;
    process->setUnixProcessParameters(parameters);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SAL_USE_VCLPLUGIN"), QStringLiteral("svp"));
    environment.remove(QStringLiteral("LD_PRELOAD"));
    process->setProcessEnvironment(environment);
    connect(process, &QProcess::readyReadStandardOutput, this,
        [this, process] { m_log = (m_log + QString::fromUtf8(process->readAllStandardOutput())).right(4096); });
    const QString pdfDir = m_cache->filePath(QStringLiteral("pdf"));
    const QString pdf = QDir(pdfDir).filePath(QFileInfo(item.path).completeBaseName() + QStringLiteral(".pdf"));
    connect(process, &QProcess::finished, this, [this, process, pdf, item](int code, QProcess::ExitStatus status) {
        if (m_process != process)
            return;
        m_timeout.stop();
        m_process = nullptr;
        process->deleteLater();
        if (status != QProcess::NormalExit || code != 0 || !QFileInfo::exists(pdf) || QFileInfo(pdf).size() == 0) {
            const auto error = tr_("LibreOffice could not convert this document: %1").arg(m_log);
            unload();
            Q_EMIT failed(error);
            return;
        }
        PreviewItem result = item;
        result.path = pdf;
        result.type = detectFileType(pdf);
        m_pdf->load(result);
    });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            unload();
            Q_EMIT failed(tr_("LibreOffice could not be started"));
        }
    });
    m_timeout.start();
    process->start(office,
        { QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(profile).toString(), QStringLiteral("--headless"),
            QStringLiteral("--nologo"), QStringLiteral("--nodefault"), QStringLiteral("--nofirststartwizard"),
            QStringLiteral("--norestore"), QStringLiteral("--convert-to"), QStringLiteral("pdf:writer_pdf_Export"),
            QStringLiteral("--outdir"), pdfDir, QFileInfo(item.path).absoluteFilePath() });
}
}
