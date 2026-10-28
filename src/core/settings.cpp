// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "settings.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace KDEGlance
{

namespace
{
constexpr auto General = "general"_L1;
constexpr auto Window = "window"_L1;
constexpr auto Media = "media"_L1;
constexpr auto Image = "image"_L1;

// Sizes outside this range are treated as garbage.
constexpr int MinSavedSide = 120;
constexpr int MaxSavedSide = 32768;

bool writeAtomically(const QString &path, const QByteArray &data, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    if (file.write(data) != data.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}
} // namespace

QString sizeCategoryKey(SizeCategory category)
{
    switch (category) {
    case SizeCategory::Image:
        return u"image"_s;
    case SizeCategory::Video:
        return u"video"_s;
    case SizeCategory::Audio:
        return u"audio"_s;
    case SizeCategory::Document: return u"document"_s;
    case SizeCategory::Pdf: return u"pdf"_s;
    case SizeCategory::Text: return u"text"_s;
    case SizeCategory::Office: return u"office"_s;
    }
    return u"image"_s;
}

Settings::Settings(const QString &filePath)
    : m_filePath(filePath)
    , m_data(defaults())
{
}

QString Settings::defaultFilePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + u"/.config"_s;
    }
    return base + u"/kdeglance/settings.json"_s;
}

QString Settings::filePath() const
{
    return m_filePath;
}

QJsonObject Settings::defaults()
{
    return QJsonObject{
        {u"schema"_s, SchemaVersion},
        {General,
         QJsonObject{
             {u"syncSelection"_s, true},
             {u"shortcut"_s, u"Space"_s},
             {u"closeOnFocusLoss"_s, true},
             {u"prestartService"_s, true},
         }},
        {Window,
         QJsonObject{
             {u"sizes"_s, QJsonObject{}},
             {u"autoSizeFraction"_s, 0.7},
         }},
        {Media,
         QJsonObject{
             {u"volume"_s, 0.8},
             {u"muted"_s, false},
             {u"autoplay"_s, true},
             {u"loopVideo"_s, false},
             {u"hardwareDecoding"_s, true},
         }},
        {Image,
         QJsonObject{
             {u"rawPreferEmbeddedPreview"_s, true},
         }},
    };
}

std::optional<QJsonObject> Settings::readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    // A settings file is tiny; refuse absurd sizes instead of parsing them.
    if (file.size() > 1024 * 1024) {
        return std::nullopt;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

QJsonObject Settings::mergeWithDefaults(const QJsonObject &stored)
{
    QJsonObject result = defaults();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        const QJsonValue def = result.value(it.key());
        if (def.isObject() && it.value().isObject()) {
            QJsonObject group = def.toObject();
            const QJsonObject storedGroup = it.value().toObject();
            for (auto g = storedGroup.constBegin(); g != storedGroup.constEnd(); ++g) {
                group.insert(g.key(), g.value());
            }
            result.insert(it.key(), group);
        } else if (it.key() != u"schema"_s) {
            // Unknown or scalar entries are kept verbatim (forward compatibility).
            result.insert(it.key(), it.value());
        }
    }
    // Copy legacy shared document geometry once. Resetting one new category
    // must never resurrect the legacy value or alter its neighbours.
    auto window = result.value(Window).toObject();
    auto sizes = window.value(u"sizes"_s).toObject();
    if (sizes.contains(u"document"_s) && stored.value(u"schema"_s).toInt(SchemaVersion)<=SchemaVersion) {
        for (auto c : {SizeCategory::Pdf, SizeCategory::Text, SizeCategory::Office})
            if (!sizes.contains(sizeCategoryKey(c))) sizes.insert(sizeCategoryKey(c), sizes.value(u"document"_s));
        sizes.remove(u"document"_s);
        window.insert(u"sizes"_s, sizes); result.insert(Window, window);
    }
    const int storedSchema = stored.value(u"schema"_s).toInt(SchemaVersion);
    if (storedSchema <= SchemaVersion) {
        // Retire the 1.0.0-5 persistent zoom; keep independent window sizes.
        auto image = result.value(Image).toObject();
        image.remove(u"zoom"_s);
        result.insert(Image, image);
    }
    result.insert(u"schema"_s, std::max(storedSchema, SchemaVersion));
    return result;
}

QJsonObject Settings::migrate(const QJsonObject &object, int fromVersion)
{
    QJsonObject result = object;
    // Schema 0 was never released; it is accepted as "flat" pre-release data:
    // {"syncSelection": bool, "sizes": {...}}.
    if (fromVersion < 1) {
        QJsonObject general = result.value(General).toObject();
        if (result.contains(u"syncSelection"_s)) {
            general.insert(u"syncSelection"_s, result.take(u"syncSelection"_s));
        }
        QJsonObject window = result.value(Window).toObject();
        if (result.contains(u"sizes"_s)) {
            window.insert(u"sizes"_s, result.take(u"sizes"_s));
        }
        result.insert(General, general);
        result.insert(Window, window);
        result.insert(u"schema"_s, 1);
    }
    return result;
}

Settings::LoadStatus Settings::load()
{
    m_data = defaults();
    const QFileInfo info(m_filePath);
    const QString backupPath = m_filePath + u".bak"_s;

    if (!info.exists()) {
        // The main file may have been lost between rename steps; use the backup.
        if (auto backup = readObject(backupPath)) {
            m_data = mergeWithDefaults(migrate(*backup, backup->value(u"schema"_s).toInt(0)));
            return LoadStatus::RecoveredFromBackup;
        }
        return LoadStatus::Missing;
    }

    if (auto stored = readObject(m_filePath)) {
        const int version = stored->value(u"schema"_s).toInt(0);
        const bool needsMigration = version < SchemaVersion
            || (version == SchemaVersion && (stored->value(Image).toObject().contains(u"zoom"_s)
                || stored->value(Window).toObject().value(u"sizes"_s).toObject().contains(u"document"_s)));
        m_data = mergeWithDefaults(needsMigration ? migrate(*stored, version) : *stored);
        return needsMigration ? LoadStatus::Migrated : LoadStatus::Ok;
    }

    // Damaged: keep the evidence for the user, never delete it.
    const QString corruptPath = m_filePath + u".corrupt"_s;
    QFile::remove(corruptPath);
    QFile::rename(m_filePath, corruptPath);

    if (auto backup = readObject(backupPath)) {
        m_data = mergeWithDefaults(migrate(*backup, backup->value(u"schema"_s).toInt(0)));
        return LoadStatus::RecoveredFromBackup;
    }
    return LoadStatus::ResetCorrupt;
}

bool Settings::save(QString *errorMessage)
{
    const QFileInfo info(m_filePath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorMessage) {
            *errorMessage = u"Cannot create %1"_s.arg(info.absolutePath());
        }
        return false;
    }
    const QByteArray data = QJsonDocument(m_data).toJson(QJsonDocument::Indented);
    if (!writeAtomically(m_filePath, data, errorMessage)) {
        return false;
    }
    // The backup is refreshed only after the main file is safely on disk.
    writeAtomically(m_filePath + u".bak"_s, data, nullptr);
    return true;
}

void Settings::resetToDefaults()
{
    // Unknown top-level keys written by newer versions are dropped as well:
    // a reset means "factory state of this version".
    m_data = defaults();
}

QJsonObject Settings::toJson() const
{
    return m_data;
}

QJsonValue Settings::value(const QString &group, const QString &key) const
{
    return m_data.value(group).toObject().value(key);
}

void Settings::setValue(const QString &group, const QString &key, const QJsonValue &value)
{
    QJsonObject g = m_data.value(group).toObject();
    g.insert(key, value);
    m_data.insert(group, g);
}

bool Settings::boolValue(const QString &group, const QString &key) const
{
    const QJsonValue v = value(group, key);
    if (v.isBool()) {
        return v.toBool();
    }
    return defaults().value(group).toObject().value(key).toBool();
}

double Settings::doubleValue(const QString &group, const QString &key, double min, double max) const
{
    const QJsonValue v = value(group, key);
    const double def = defaults().value(group).toObject().value(key).toDouble();
    if (!v.isDouble() || !std::isfinite(v.toDouble())) {
        return def;
    }
    const double d = v.toDouble();
    if (d < min || d > max) {
        return def;
    }
    return d;
}

bool Settings::syncSelection() const
{
    return boolValue(General, u"syncSelection"_s);
}

void Settings::setSyncSelection(bool enabled)
{
    setValue(General, u"syncSelection"_s, enabled);
}

QKeySequence Settings::shortcut() const
{
    const QString text = value(General, u"shortcut"_s).toString();
    QKeySequence seq = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (text.isEmpty() || seq.isEmpty() || seq.count() != 1) {
        seq = QKeySequence(Qt::Key_Space);
    }
    return seq;
}

void Settings::setShortcut(const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        setValue(General, u"shortcut"_s, u"Space"_s);
        return;
    }
    // Only the first chord is meaningful for a file-manager hotkey.
    setValue(General, u"shortcut"_s, QKeySequence(sequence[0]).toString(QKeySequence::PortableText));
}

bool Settings::closeOnFocusLoss() const
{
    return boolValue(General, u"closeOnFocusLoss"_s);
}

void Settings::setCloseOnFocusLoss(bool enabled)
{
    setValue(General, u"closeOnFocusLoss"_s, enabled);
}

bool Settings::prestartService() const
{
    return boolValue(General, u"prestartService"_s);
}

void Settings::setPrestartService(bool enabled)
{
    setValue(General, u"prestartService"_s, enabled);
}

std::optional<QSize> Settings::savedSize(SizeCategory category) const
{
    const QJsonObject sizes = value(Window, u"sizes"_s).toObject();
    const QJsonValue entry = sizes.value(sizeCategoryKey(category));
    if (!entry.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = entry.toObject();
    const QJsonValue w = o.value(u"w"_s);
    const QJsonValue h = o.value(u"h"_s);
    if (!w.isDouble() || !h.isDouble()) {
        return std::nullopt;
    }
    const int iw = w.toInt();
    const int ih = h.toInt();
    if (iw < (category==SizeCategory::Image?1:MinSavedSide) || ih < (category==SizeCategory::Image?1:MinSavedSide) || iw > MaxSavedSide || ih > MaxSavedSide) {
        return std::nullopt;
    }
    return QSize(iw, ih);
}

void Settings::setSavedSize(SizeCategory category, const QSize &size)
{
    if (!size.isValid() || size.width() < (category==SizeCategory::Image?1:MinSavedSide) || size.height() < (category==SizeCategory::Image?1:MinSavedSide)) {
        return;
    }
    QJsonObject sizes = value(Window, u"sizes"_s).toObject();
    sizes.insert(sizeCategoryKey(category),
                 QJsonObject{{u"w"_s, std::min(size.width(), MaxSavedSide)}, {u"h"_s, std::min(size.height(), MaxSavedSide)}});
    setValue(Window, u"sizes"_s, sizes);
}

void Settings::clearSavedSizes()
{
    setValue(Window, u"sizes"_s, QJsonObject{});
}

double Settings::autoSizeFraction() const
{
    return doubleValue(Window, u"autoSizeFraction"_s, 0.3, 0.95);
}

void Settings::resetModule(SizeCategory category)
{
    auto sizes = value(Window, u"sizes"_s).toObject();
    sizes.remove(sizeCategoryKey(category));
    setValue(Window, u"sizes"_s, sizes);
    const auto key = sizeCategoryKey(category);
    if (category == SizeCategory::Image) m_data.insert(key, defaults().value(Image));
    else if (category == SizeCategory::Audio || category == SizeCategory::Video) {
        // Explicit defaults isolate this reset even when legacy media values differ.
        auto group = defaults().value(Media).toObject();
        group.remove(u"hardwareDecoding"_s); // shared process-level decoder policy
        group.remove(u"autoplay"_s); // shared application policy is not a display parameter
        if (category == SizeCategory::Audio) group.remove(u"loopVideo"_s);
        m_data.insert(key, group);
    } else m_data.remove(key);
}
double Settings::moduleVolume(SizeCategory c) const
{
    const auto v=value(sizeCategoryKey(c),u"volume"_s);
    return v.isDouble() && std::isfinite(v.toDouble()) && v.toDouble()>=0 && v.toDouble()<=1 ? v.toDouble() : volume();
}
bool Settings::moduleMuted(SizeCategory c) const { const auto v=value(sizeCategoryKey(c),u"muted"_s); return v.isBool()?v.toBool():muted(); }
bool Settings::moduleLoopVideo() const { const auto v=value(u"video"_s,u"loopVideo"_s); return v.isBool()?v.toBool():loopVideo(); }
void Settings::setModuleVolume(SizeCategory c,double v) { if(std::isfinite(v)) setValue(sizeCategoryKey(c),u"volume"_s,std::clamp(v,0.0,1.0)); }
void Settings::setModuleMuted(SizeCategory c,bool v) { setValue(sizeCategoryKey(c),u"muted"_s,v); }
void Settings::setModuleLoopVideo(bool v) { setValue(u"video"_s,u"loopVideo"_s,v); }

double Settings::volume() const
{
    return doubleValue(Media, u"volume"_s, 0.0, 1.0);
}

void Settings::setVolume(double volume)
{
    setValue(Media, u"volume"_s, std::clamp(volume, 0.0, 1.0));
}

bool Settings::muted() const
{
    return boolValue(Media, u"muted"_s);
}

void Settings::setMuted(bool muted)
{
    setValue(Media, u"muted"_s, muted);
}

bool Settings::autoplay() const
{
    return boolValue(Media, u"autoplay"_s);
}

void Settings::setAutoplay(bool enabled)
{
    setValue(Media, u"autoplay"_s, enabled);
}

bool Settings::loopVideo() const
{
    return boolValue(Media, u"loopVideo"_s);
}

void Settings::setLoopVideo(bool enabled)
{
    setValue(Media, u"loopVideo"_s, enabled);
}

bool Settings::hardwareDecoding() const
{
    return boolValue(Media, u"hardwareDecoding"_s);
}

void Settings::setHardwareDecoding(bool enabled)
{
    setValue(Media, u"hardwareDecoding"_s, enabled);
}

bool Settings::rawPreferEmbeddedPreview() const
{
    return boolValue(Image, u"rawPreferEmbeddedPreview"_s);
}

void Settings::setRawPreferEmbeddedPreview(bool enabled)
{
    setValue(Image, u"rawPreferEmbeddedPreview"_s, enabled);
}

} // namespace KDEGlance
