// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#pragma once

#include <QJsonObject>
#include <QKeySequence>
#include <QSize>
#include <QString>

#include <optional>

namespace KDEGlance
{

// Window size memory is kept separately for these content groups (spec 5.4).
enum class SizeCategory {
    Image, // static images, RAW, SVG, GIF and other animations
    Video,
    Audio,
    Document, // legacy geometry, migrated independently to each document module
    Pdf,
    Text,
    Office,
};

QString sizeCategoryKey(SizeCategory category);

/*
 * User settings of KDEGlance.
 *
 * Storage: $XDG_CONFIG_HOME/kdeglance/settings.json (JSON, versioned schema).
 *  - Every value has a default; a missing or wrongly typed value silently
 *    falls back to the default instead of failing.
 *  - save() is atomic (QSaveFile: write to a temp file, fsync, rename) and
 *    refreshes settings.json.bak, the last known good copy.
 *  - A damaged settings.json is moved aside to settings.json.corrupt and the
 *    backup (or, failing that, the defaults) is used.
 *  - Unknown keys are preserved, so a newer KDEGlance does not lose data when
 *    an older one rewrites the file.
 *  - resetToDefaults() only touches KDEGlance's own file.
 *
 * Only the viewer service writes the file; the Dolphin hook reads it and is
 * told about changes over D-Bus.
 */
class Settings
{
public:
    static constexpr int SchemaVersion = 1;

    enum class LoadStatus {
        Ok,
        Missing, // no file yet: defaults are in use
        Migrated, // older schema upgraded in memory
        RecoveredFromBackup, // main file was damaged, backup used
        ResetCorrupt, // main file and backup damaged, defaults used
    };

    explicit Settings(const QString &filePath = defaultFilePath());

    static QString defaultFilePath();
    QString filePath() const;

    LoadStatus load();
    bool save(QString *errorMessage = nullptr);
    void resetToDefaults();
    void resetModule(SizeCategory category);
    double moduleVolume(SizeCategory category) const;
    bool moduleMuted(SizeCategory category) const;
    bool moduleLoopVideo() const;
    void setModuleVolume(SizeCategory category, double value);
    void setModuleMuted(SizeCategory category, bool value);
    void setModuleLoopVideo(bool value);

    // --- general -----------------------------------------------------------
    bool syncSelection() const;
    void setSyncSelection(bool enabled);

    // Portable QKeySequence text, e.g. "Space" or "Ctrl+Y".
    QKeySequence shortcut() const;
    void setShortcut(const QKeySequence &sequence);

    bool closeOnFocusLoss() const;
    void setCloseOnFocusLoss(bool enabled);

    // Start the viewer service in the background as soon as Dolphin starts,
    // so that the first preview opens instantly.
    bool prestartService() const;
    void setPrestartService(bool enabled);

    // --- window --------------------------------------------------------------
    std::optional<QSize> savedSize(SizeCategory category) const;
    void setSavedSize(SizeCategory category, const QSize &size);
    void clearSavedSizes();

    // Fraction of the available work area used by the automatic size.
    double autoSizeFraction() const;

    // --- media ----------------------------------------------------------------
    double volume() const; // 0..1
    void setVolume(double volume);
    bool muted() const;
    void setMuted(bool muted);
    bool autoplay() const;
    void setAutoplay(bool enabled);
    bool loopVideo() const;
    void setLoopVideo(bool enabled);
    bool hardwareDecoding() const;
    void setHardwareDecoding(bool enabled);

    // --- image -----------------------------------------------------------------
    bool rawPreferEmbeddedPreview() const;
    void setRawPreferEmbeddedPreview(bool enabled);

    QJsonObject toJson() const;
    static QJsonObject defaults();

    // Exposed for tests.
    static QJsonObject migrate(const QJsonObject &object, int fromVersion);

private:
    QJsonValue value(const QString &group, const QString &key) const;
    void setValue(const QString &group, const QString &key, const QJsonValue &value);
    bool boolValue(const QString &group, const QString &key) const;
    double doubleValue(const QString &group, const QString &key, double min, double max) const;

    static std::optional<QJsonObject> readObject(const QString &path);
    static QJsonObject mergeWithDefaults(const QJsonObject &stored);

    QString m_filePath;
    QJsonObject m_data;
};

} // namespace KDEGlance
