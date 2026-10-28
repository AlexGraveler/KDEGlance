// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace KDEGlance;
using namespace Qt::StringLiterals;

class SettingsTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString path() const
    {
        return m_dir.filePath(u"kdeglance/settings.json"_s);
    }
    void writeRaw(const QString &file, const QByteArray &data)
    {
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

private Q_SLOTS:
    void init()
    {
        QDir(m_dir.path()).removeRecursively();
        QDir().mkpath(m_dir.path());
    }

    void defaults()
    {
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::Missing);
        QVERIFY(s.syncSelection());
        QCOMPARE(s.shortcut(), QKeySequence(Qt::Key_Space));
        QVERIFY(s.closeOnFocusLoss());
        QVERIFY(!s.savedSize(SizeCategory::Image).has_value());
        QCOMPARE(s.autoSizeFraction(), 0.7);
        QCOMPARE(s.volume(), 0.8);
        QVERIFY(s.hardwareDecoding());
    }

    void roundTrip()
    {
        Settings s(path());
        s.setSyncSelection(false);
        s.setShortcut(QKeySequence(u"Ctrl+Y"_s));
        s.setSavedSize(SizeCategory::Video, QSize(1000, 600));
        s.setSavedSize(SizeCategory::Audio, QSize(420, 430));
        s.setVolume(0.33);
        QVERIFY(s.save());
        QVERIFY(QFile::exists(path()));
        QVERIFY(QFile::exists(path() + u".bak"_s));

        Settings t(path());
        QCOMPARE(t.load(), Settings::LoadStatus::Ok);
        QVERIFY(!t.syncSelection());
        QCOMPARE(t.shortcut(), QKeySequence(u"Ctrl+Y"_s));
        QCOMPARE(t.savedSize(SizeCategory::Video), std::optional<QSize>(QSize(1000, 600)));
        QCOMPARE(t.savedSize(SizeCategory::Audio), std::optional<QSize>(QSize(420, 430)));
        QVERIFY(!t.savedSize(SizeCategory::Image).has_value());
        QCOMPARE(t.volume(), 0.33);
        QCOMPARE(t.toJson().value(u"schema"_s).toInt(), Settings::SchemaVersion);
    }

    void moduleResetIsIndependentAndPersistent()
    {
        const QList<SizeCategory> modules{SizeCategory::Image,SizeCategory::Video,SizeCategory::Audio,SizeCategory::Pdf,SizeCategory::Text,SizeCategory::Office};
        for(auto active:modules) {
            Settings s(path()); s.setSyncSelection(false); s.setVolume(.31); s.setMuted(true); s.setAutoplay(false); s.setLoopVideo(true); s.setRawPreferEmbeddedPreview(false);
            for(auto c:modules) s.setSavedSize(c,QSize(500+int(c)*10,400+int(c)*10));
            s.setModuleVolume(SizeCategory::Audio,.2); s.setModuleVolume(SizeCategory::Video,.7);
            const auto before=s.toJson(); s.resetModule(active);
            QVERIFY(!s.savedSize(active)); QVERIFY(!s.syncSelection()); QCOMPARE(s.volume(),.31); QCOMPARE(s.muted(),true);
            for(auto c:modules) if(c!=active) {
                QCOMPARE(s.savedSize(c),std::optional<QSize>(QSize(500+int(c)*10,400+int(c)*10)));
                QCOMPARE(s.toJson().value(sizeCategoryKey(c)),before.value(sizeCategoryKey(c)));
            }
            if(active==SizeCategory::Audio || active==SizeCategory::Video) {
                QCOMPARE(s.moduleVolume(active),.8); QVERIFY(!s.moduleMuted(active)); QVERIFY(!s.autoplay());
            }
            QVERIFY(s.save()); Settings loaded(path()); loaded.load();
            QCOMPARE(loaded.toJson(),s.toJson()); QVERIFY(!loaded.savedSize(active));
        }
    }
    void legacyDocumentsMigrateWithoutResurrectingReset()
    {
        writeRaw(path(),R"({"schema":1,"window":{"sizes":{"document":{"w":620,"h":710}}}})");
        Settings s(path()); QCOMPARE(s.load(),Settings::LoadStatus::Migrated);
        for(auto c:{SizeCategory::Text,SizeCategory::Pdf,SizeCategory::Office}) QCOMPARE(s.savedSize(c),std::optional<QSize>(QSize(620,710)));
        s.resetModule(SizeCategory::Pdf); QVERIFY(s.save()); Settings loaded(path()); loaded.load();
        QVERIFY(!loaded.savedSize(SizeCategory::Pdf)); QCOMPARE(loaded.savedSize(SizeCategory::Text),std::optional<QSize>(QSize(620,710)));
        QCOMPARE(loaded.savedSize(SizeCategory::Office),std::optional<QSize>(QSize(620,710)));
    }

    void sizesAreSeparatePerCategory()
    {
        Settings s(path());
        s.setSavedSize(SizeCategory::Image, QSize(700, 500));
        QVERIFY(!s.savedSize(SizeCategory::Video));
        QVERIFY(!s.savedSize(SizeCategory::Audio));
        s.clearSavedSizes();
        QVERIFY(!s.savedSize(SizeCategory::Image));
    }

    void invalidValuesFallBack()
    {
        writeRaw(path(), R"({"schema":1,"general":{"syncSelection":"yes","shortcut":"Ctrl+Shift+A,B"},
                         "window":{"sizes":{"image":{"w":0,"h":5},"video":{"w":"x"}}, "autoSizeFraction": 7},
                         "media":{"volume":-3}})");
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::Ok);
        QVERIFY(s.syncSelection()); // wrong type → default
        QCOMPARE(s.shortcut(), QKeySequence(Qt::Key_Space)); // multi-chord → default
        QVERIFY(!s.savedSize(SizeCategory::Image)); // too small
        QVERIFY(!s.savedSize(SizeCategory::Video)); // wrong type
        QCOMPARE(s.autoSizeFraction(), 0.7);
        QCOMPARE(s.volume(), 0.8);
    }

    void corruptFileRecoversFromBackup()
    {
        Settings s(path());
        s.setSyncSelection(false);
        QVERIFY(s.save());
        writeRaw(path(), "{ this is not json");
        Settings t(path());
        QCOMPARE(t.load(), Settings::LoadStatus::RecoveredFromBackup);
        QVERIFY(!t.syncSelection());
        QVERIFY(QFile::exists(path() + u".corrupt"_s)); // evidence kept, not deleted
    }

    void corruptFileAndBackupResetToDefaults()
    {
        writeRaw(path(), "garbage");
        writeRaw(path() + u".bak"_s, "\x01\x02");
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::ResetCorrupt);
        QVERIFY(s.syncSelection());
        QVERIFY(s.save());
        Settings t(path());
        QCOMPARE(t.load(), Settings::LoadStatus::Ok);
    }

    void migratesSchema0()
    {
        writeRaw(path(), R"({"syncSelection":false,"sizes":{"audio":{"w":500,"h":510}}})");
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::Migrated);
        QVERIFY(!s.syncSelection());
        QCOMPARE(s.savedSize(SizeCategory::Audio), std::optional<QSize>(QSize(500, 510)));
        QCOMPARE(s.toJson().value(u"schema"_s).toInt(), 1);
    }

    void unknownKeysArePreserved()
    {
        writeRaw(path(), R"({"schema":1,"future":{"x":1},"general":{"newOption":42}})");
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::Ok);
        s.setSyncSelection(false);
        QVERIFY(s.save());
        QFile f(path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(o.value(u"future"_s).toObject().value(u"x"_s).toInt(), 1);
        QCOMPARE(o.value(u"general"_s).toObject().value(u"newOption"_s).toInt(), 42);
    }

    void newerSchemaIsKept()
    {
        writeRaw(path(), R"({"schema":7,"general":{"syncSelection":false}})");
        Settings s(path());
        QCOMPARE(s.load(), Settings::LoadStatus::Ok);
        QVERIFY(!s.syncSelection());
        QCOMPARE(s.toJson().value(u"schema"_s).toInt(), 7);
    }

    void resetRestoresDefaultsOnlyInOwnFile()
    {
        const QString neighbour = m_dir.filePath(u"kdeglance/other.txt"_s);
        writeRaw(neighbour, "keep me");
        Settings s(path());
        s.setSyncSelection(false);
        s.setSavedSize(SizeCategory::Image, QSize(900, 700));
        QVERIFY(s.save());
        s.resetToDefaults();
        QVERIFY(s.save());
        Settings t(path());
        t.load();
        QVERIFY(t.syncSelection());
        QVERIFY(!t.savedSize(SizeCategory::Image));
        QVERIFY(QFile::exists(neighbour));
    }

    void saveIsAtomic()
    {
        // A read-only directory makes the save fail without touching the old file.
        Settings s(path());
        QVERIFY(s.save());
        QFile::setPermissions(QFileInfo(path()).absolutePath(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        s.setSyncSelection(false);
        QString error;
        const bool ok = s.save(&error);
        QFile::setPermissions(QFileInfo(path()).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (ok) {
            QSKIP("running as root: directory permissions are not enforced");
        }
        Settings t(path());
        QCOMPARE(t.load(), Settings::LoadStatus::Ok);
        QVERIFY(t.syncSelection());
    }
};

QTEST_GUILESS_MAIN(SettingsTest)
#include "settingstest.moc"
