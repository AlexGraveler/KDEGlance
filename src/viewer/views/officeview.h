// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "contentview.h"
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
namespace KDEGlance {
class PdfView;
class OfficeView : public ContentView {
    Q_OBJECT
public:
    explicit OfficeView(QWidget* parent = nullptr);
    ~OfficeView() override;
    void load(const PreviewItem& item) override;
    void unload() override;
    void resetPresentation() override;
    SizeCategory sizeCategory() const override { return SizeCategory::Office; }
    QSize naturalSize() const override { return QSize(800, 700); }
    bool handleKey(QKeyEvent* e) override;
    PdfView* pdfView() const { return m_pdf; }
    bool converting() const { return m_process && m_process->state() != QProcess::NotRunning; }

private:
    void stopConverter();
    PdfView* m_pdf;
    QPointer<QProcess> m_process;
    std::unique_ptr<QTemporaryDir> m_cache;
    QTimer m_timeout;
    QString m_log;
};
}
