// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "settingsdialog.h"

#include "l10n.h"
#include "settings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

SettingsDialog::SettingsDialog(Settings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_shortcut(new QKeySequenceEdit(this))
    , m_sync(new QCheckBox(tr_("Select the previewed file in Dolphin"), this))
    , m_focusClose(new QCheckBox(tr_("Close the preview when it loses focus"), this))
    , m_prestart(new QCheckBox(tr_("Start KDEGlance in the background together with Dolphin (faster first preview)"), this))
    , m_autoplay(new QCheckBox(tr_("Start playing audio and video automatically"), this))
    , m_loop(new QCheckBox(tr_("Repeat videos"), this))
    , m_hwdec(new QCheckBox(tr_("Use hardware video decoding when available (applies after restart)"), this))
    , m_rawEmbedded(new QCheckBox(tr_("Show the camera's embedded preview of RAW photos first (much faster)"), this))
    , m_sizes(new QLabel(this))
{
    setWindowTitle(tr_("KDEGlance Settings"));
    setWindowIcon(QIcon::fromTheme(u"org.kde.kdeglance"_s, QIcon::fromTheme(u"document-preview"_s)));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *shortcutRow = new QHBoxLayout;
    m_shortcut->setMaximumSequenceLength(1);
    shortcutRow->addWidget(m_shortcut, 1);
    auto *defaultKey = new QToolButton(this);
    defaultKey->setText(tr_("Default (Space)"));
    shortcutRow->addWidget(defaultKey);
    form->addRow(tr_("Quick Look key in Dolphin:"), shortcutRow);
    layout->addLayout(form);

    for (QCheckBox *box : {m_sync, m_focusClose, m_prestart, m_autoplay, m_loop, m_hwdec, m_rawEmbedded}) {
        layout->addWidget(box);
        connect(box, &QCheckBox::toggled, this, &SettingsDialog::commit);
    }

    auto *sizesRow = new QHBoxLayout;
    sizesRow->addWidget(m_sizes, 1);
    auto *forget = new QPushButton(tr_("Forget window sizes"), this);
    sizesRow->addWidget(forget);
    layout->addLayout(sizesRow);

    auto *hint = new QLabel(tr_("Tip: a double middle click in the middle of the preview resets all KDEGlance settings."), this);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(m_shortcut, &QKeySequenceEdit::editingFinished, this, &SettingsDialog::commit);
    connect(defaultKey, &QToolButton::clicked, this, [this] {
        m_shortcut->setKeySequence(QKeySequence(Qt::Key_Space));
        commit();
    });
    connect(forget, &QPushButton::clicked, this, [this] {
        m_settings->clearSavedSizes();
        load();
        Q_EMIT changed();
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        m_settings->resetToDefaults();
        load();
        Q_EMIT changed();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    load();
}

SettingsDialog::~SettingsDialog()
{
    // QKeySequenceEdit emits editingFinished when it loses focus while the
    // dialog is being destroyed; this object must not be called back then.
    disconnect(m_shortcut, nullptr, this, nullptr);
    m_loading = true;
}

void SettingsDialog::load()
{
    m_loading = true;
    m_shortcut->setKeySequence(m_settings->shortcut());
    m_sync->setChecked(m_settings->syncSelection());
    m_focusClose->setChecked(m_settings->closeOnFocusLoss());
    m_prestart->setChecked(m_settings->prestartService());
    m_autoplay->setChecked(m_settings->autoplay());
    m_loop->setChecked(m_settings->moduleLoopVideo());
    m_hwdec->setChecked(m_settings->hardwareDecoding());
    m_rawEmbedded->setChecked(m_settings->rawPreferEmbeddedPreview());
    QStringList sizes;
    for (SizeCategory c : {SizeCategory::Image, SizeCategory::Video, SizeCategory::Audio, SizeCategory::Document, SizeCategory::Pdf, SizeCategory::Text, SizeCategory::Office}) {
        if (auto s = m_settings->savedSize(c)) {
            const QString name = c == SizeCategory::Image ? tr_("images") : c == SizeCategory::Video ? tr_("video") : c == SizeCategory::Audio ? tr_("audio") : c==SizeCategory::Pdf ? tr_("PDF") : c==SizeCategory::Text ? tr_("text") : c==SizeCategory::Office ? tr_("office documents") : tr_("documents");
            sizes << u"%1 %2×%3"_s.arg(name).arg(s->width()).arg(s->height());
        }
    }
    m_sizes->setText(sizes.isEmpty() ? tr_("Remembered window sizes: none") : tr_("Remembered window sizes: %1").arg(sizes.join(u", "_s)));
    m_loading = false;
}

void SettingsDialog::commit()
{
    if (m_loading) {
        return;
    }
    QKeySequence seq = m_shortcut->keySequence();
    if (seq.isEmpty()) {
        seq = QKeySequence(Qt::Key_Space);
    }
    m_settings->setShortcut(seq);
    m_settings->setSyncSelection(m_sync->isChecked());
    m_settings->setCloseOnFocusLoss(m_focusClose->isChecked());
    m_settings->setPrestartService(m_prestart->isChecked());
    m_settings->setAutoplay(m_autoplay->isChecked());
    m_settings->setModuleLoopVideo(m_loop->isChecked());
    m_settings->setHardwareDecoding(m_hwdec->isChecked());
    m_settings->setRawPreferEmbeddedPreview(m_rawEmbedded->isChecked());
    Q_EMIT changed();
}

} // namespace KDEGlance
