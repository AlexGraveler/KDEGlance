// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QDialog>

class QCheckBox;
class QButtonGroup;
class QKeySequenceEdit;
class QLabel;

namespace KDEGlance
{
class Settings;

// KDEGlance settings. Changes apply immediately (no OK/Apply).
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(Settings *settings, QWidget *parent = nullptr);
    ~SettingsDialog() override;

Q_SIGNALS:
    void changed();

private:
    void load();
    void commit();

    Settings *m_settings;
    QKeySequenceEdit *m_shortcut;
    QCheckBox *m_sync;
    QCheckBox *m_focusClose;
    QCheckBox *m_prestart;
    QCheckBox *m_autoplay;
    QCheckBox *m_hwdec;
    QCheckBox *m_rawEmbedded;
    QLabel *m_sizes;
    QCheckBox *m_syntax;
    QButtonGroup *m_filename;
    bool m_loading = false;
};

} // namespace KDEGlance
