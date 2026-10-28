// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "previewwindow.h"
#include "settingsdialog.h"
#include "viewerapp.h"
#include "views/imageloader.h"
#include "views/imageview.h"

#include "../testutils.h"

#include <QApplication>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QPointer>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>

using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class ViewerAppTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QStringList m_urls;

    QJsonObject status(ViewerApp &app)
    {
        return QJsonDocument::fromJson(app.Status().toUtf8()).object();
    }

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(u"config"_s).toUtf8());
        // Deliberately not alphabetical: the order given by Dolphin rules.
        for (const QString &name : {u"c.png"_s, u"a.png"_s, u"b.png"_s}) {
            QVERIFY(twoColorImage(320, 200).save(m_dir.filePath(name)));
            m_urls << QUrl::fromLocalFile(m_dir.filePath(name)).toString();
        }
    }

    void lateCompositorReportCannotAffectNewSession()
    {
        ViewerApp app;app.settings()->resetModule(SizeCategory::Image);
        app.Open(m_urls,0,{{u"session"_s,u"fresh"_s}});
        QVERIFY(waitFor([&]{return app.window()->imageView()->hasImage() && app.window()->isVisible();}));
        auto* view=app.window()->imageView();
        const auto wheel=[&] {
            QWheelEvent e(QPointF(100,80),view->mapToGlobal(QPoint(100,80)),{},QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(view,&e);
        };
        const auto size=app.window()->size();
        app.ReportPhotoWindowState(u"closed-session"_s,true);wheel();
        QVERIFY(app.window()->width()<size.width());
        const auto capped=app.window()->size();const auto scale=view->zoomScale();
        app.ReportPhotoWindowState(u"fresh"_s,true);wheel();
        QCOMPARE(app.window()->size(),capped);QVERIFY(view->zoomScale()<scale);
        app.ReportPhotoWindowState(u"fresh"_s,false);wheel();
        QVERIFY(app.window()->width()<capped.width());app.Close();
    }

    void openNavigateClose()
    {
        ViewerApp app;
        QSignalSpy current(&app, &ViewerApp::CurrentChanged);
        QSignalSpy closed(&app, &ViewerApp::Closed);
        app.Open(m_urls, 1, {{u"session"_s, u"test-1"_s}});
        QVERIFY(waitFor([&] {
            return app.IsVisible() && app.window()->isVisible();
        }));
        QCOMPARE(app.CurrentUrl(), m_urls.at(1));
        app.Navigate(1);
        QCOMPARE(app.CurrentUrl(), m_urls.at(2));
        QCOMPARE(current.size(), 1);
        QCOMPARE(current.first().at(0).toString(), u"test-1"_s);
        QCOMPARE(current.first().at(1).toString(), m_urls.at(2));
        app.Navigate(1); // already last: nothing happens
        QCOMPARE(current.size(), 1);
        app.Navigate(-1);
        app.Navigate(-1);
        QCOMPARE(app.CurrentUrl(), m_urls.at(0));
        app.Close();
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.first().at(0).toString(), u"test-1"_s);
        QCOMPARE(closed.first().at(1).toString(), m_urls.at(0));
        QVERIFY(!app.IsVisible());
    }

    void toggleClosesSameFile()
    {
        ViewerApp app;
        app.Toggle(m_urls, 0, {{u"session"_s, u"s"_s}});
        QVERIFY(waitFor([&] {
            return app.IsVisible();
        }));
        app.Toggle(m_urls, 0, {{u"session"_s, u"s"_s}});
        QVERIFY(!app.IsVisible());
    }

    void newRequestReplacesSession()
    {
        ViewerApp app;
        QSignalSpy closed(&app, &ViewerApp::Closed);
        app.Open(m_urls, 0, {{u"session"_s, u"first"_s}});
        app.Open(m_urls, 2, {{u"session"_s, u"second"_s}});
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.first().first().toString(), u"first"_s);
        QCOMPARE(app.session(), u"second"_s);
        QCOMPARE(app.CurrentUrl(), m_urls.at(2));
        app.Close();
    }

    void updateListKeepsPosition()
    {
        ViewerApp app;
        app.Open(m_urls, 1, {{u"session"_s, u"u"_s}});
        QStringList reordered{m_urls.at(2), m_urls.at(1), m_urls.at(0)};
        app.UpdateList(u"other-session"_s, {m_urls.at(0)}); // ignored
        QCOMPARE(app.navigation().count(), 3);
        app.UpdateList(u"u"_s, reordered);
        QCOMPARE(app.navigation().index(), 1);
        app.Navigate(1);
        QCOMPARE(app.CurrentUrl(), m_urls.at(0));
        app.Close();
    }

    void cliOpenListsFolder()
    {
        ViewerApp app;
        app.Open({m_urls.at(1)}, 0, {}); // no session: local folder listing
        QVERIFY(waitFor([&] {
            return app.navigation().count() == 3;
        }));
        // Natural order a, b, c for the fallback listing.
        QCOMPARE(app.navigation().urls().first().fileName(), u"a.png"_s);
        app.Close();
    }

    void workAreaReport()
    {
        ViewerApp app;
        app.ReportWorkAreas(uR"({"active":"DP-1","screens":[{"name":"DP-1","x":0,"y":0,"width":2048,"height":1108,"scale":1.25},
                                {"name":"HDMI-A-1","x":2048,"y":0,"width":1920,"height":1080,"scale":1}]})"_s);
        QCOMPARE(app.window()->workArea(), QRect(0, 0, 2048, 1108));
        app.Open(m_urls, 0, {{u"session"_s, u"w"_s}, {u"screen"_s, u"HDMI-A-1"_s}});
        QCOMPARE(app.window()->workArea(), QRect(2048, 0, 1920, 1080));
        app.Close();
    }

    void setOptionAndStatus()
    {
        ViewerApp app;
        QSignalSpy changed(&app, &ViewerApp::SettingsChanged);
        QVERIFY(app.SetOption(u"syncSelection"_s, QDBusVariant(false)));
        QVERIFY(!app.SetOption(u"unknown"_s, QDBusVariant(1)));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(status(app).value(u"syncSelection"_s).toBool(), false);
        app.ResetSettings();
        QCOMPARE(status(app).value(u"syncSelection"_s).toBool(), true);
    }

    void settingsDialogOpensAndClosesSafely()
    {
        ViewerApp app;
        app.ShowSettings();
        QPointer<SettingsDialog> dialog;
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *d = qobject_cast<SettingsDialog *>(w)) {
                dialog = d;
            }
        }
        QVERIFY(dialog);
        QVERIFY(QTest::qWaitForWindowExposed(dialog));
        auto *edit = dialog->findChild<QKeySequenceEdit *>();
        QVERIFY(edit);
        dialog->activateWindow();
        edit->setFocus();
        QVERIFY(waitFor([&] {
            return edit->hasFocus();
        }));
        // Regression: destroying the visible dialog with the key editor
        // focused (e.g. service shutdown) must not call back into the
        // half-destroyed dialog.
        delete dialog.data();
        QVERIFY(dialog.isNull());
    }

    void rapidNavigationShowsLastFile()
    {
        ViewerApp app;
        QStringList many;
        for (int i = 0; i < 40; ++i) {
            const QString f = m_dir.filePath(u"n%1.png"_s.arg(i, 2, 10, QLatin1Char('0')));
            QImage img(200 + i * 20, 150, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(i * 9, 200, 200));
            QVERIFY(img.save(f));
            many << QUrl::fromLocalFile(f).toString();
        }
        app.Open(many, 0, {{u"session"_s, u"r"_s}});
        for (int i = 0; i < 39; ++i) {
            app.Navigate(1); // faster than decoding
        }
        QCOMPARE(app.CurrentUrl(), many.last());
        QVERIFY(waitFor([&] {
            return app.window()->imageView()->hasImage();
        }));
        app.window()->imageView()->loader()->waitForIdle();
        QTest::qWait(50);
        // The last file is shown, not a late result of an earlier one.
        QCOMPARE(app.window()->imageView()->naturalSize(), QSize(200 + 39 * 20, 150));
        app.Close();
    }
};

QTEST_MAIN(ViewerAppTest)
#include "viewerapptest.moc"
