// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "contentview.h"
#include <QTimer>
#include <memory>
class QPlainTextEdit;
class QTextBrowser;
namespace KDEGlance {
struct TextSession;
class TextView : public ContentView {
    Q_OBJECT
public:
    explicit TextView(QWidget* parent = nullptr);
    ~TextView() override;
    void load(const PreviewItem& item) override;
    void unload() override;
    void resetPresentation() override;
    SizeCategory sizeCategory() const override { return SizeCategory::Text; }
    QSize naturalSize() const override { return QSize(800, 700); }
    QPlainTextEdit* editor() const { return m_editor; }
    bool renderedMarkdown() const;
    void loadMore();
    bool handleKey(QKeyEvent* event) override;
    void toggleMarkdown();

private:
    void beginRead();
    PreviewItem m_item;
    std::shared_ptr<TextSession> m_session;
    quint64 m_generation = 0;
    QPlainTextEdit* m_editor;
    QTextBrowser* m_markdown;
    bool m_hasMore = false;
    QTimer m_appendTimer;
    QString m_pending;
    qsizetype m_appended = 0;
    bool m_reading = false;
    bool m_ready = false;
};
}
