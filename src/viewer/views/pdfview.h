// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "contentview.h"
#include <memory>
class QPlainTextEdit;
namespace KDEGlance {
struct PdfSession;
class PdfCanvas;
class PdfView : public ContentView {
    Q_OBJECT
public:
    explicit PdfView(QWidget* parent = nullptr, bool accelerateVerticalWheel = true);
    ~PdfView() override;
    void load(const PreviewItem& item) override;
    void unload() override;
    void resetPresentation() override;
    SizeCategory sizeCategory() const override { return SizeCategory::Pdf; }
    QSize naturalSize() const override { return QSize(800, 700); }
    int pageCount() const;
    int cachedPages() const;
    qreal zoom() const;
    void setZoom(qreal zoom);
    bool isReflow() const;
    bool handleKey(QKeyEvent* event) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setReflow(bool enabled);
    std::shared_ptr<PdfSession> m_session;
    quint64 m_generation = 0;
    PdfCanvas* m_canvas;
    QPlainTextEdit* m_text;
    bool m_reflowRequested = false;
    bool m_accelerateVerticalWheel;
    bool m_forwardingWheel = false;
};
}
