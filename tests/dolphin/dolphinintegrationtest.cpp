// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

// End-to-end test of the Dolphin integration: this process plays Dolphin
// (mock view classes, applicationName "dolphin") and loads the real hook and
// context-menu plugins; the real viewer runs as a separate process, started
// by D-Bus activation on a private session bus (see run-integration.sh).

#include "constants.h"
#include "mockdolphin.h"

#include "../testutils.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItemListProperties>
#include <KOverlayIconPlugin>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QAction>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

#include <signal.h>
#include <sys/stat.h>

using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;

class DolphinIntegrationTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    std::unique_ptr<DolphinMainWindow> m_window;
    DolphinView *m_view = nullptr;
    QObject *m_hook = nullptr;

    QString path(const QString &name) const
    {
        return m_dir.filePath(name);
    }
    QUrl url(const QString &name) const
    {
        return QUrl::fromLocalFile(path(name));
    }
    KFileItem fileItem(const QString &name, bool dir = false) const
    {
        return KFileItem(url(name), QString(), dir ? S_IFDIR : S_IFREG);
    }

    QJsonObject viewer(const QString &method = u"Status"_s)
    {
        QDBusInterface iface(QString(KDEGlance::DBusService), QString(KDEGlance::DBusViewerPath), QString(KDEGlance::DBusViewerInterface));
        QDBusReply<QString> reply = iface.call(method);
        return QJsonDocument::fromJson(reply.value().toUtf8()).object();
    }
    bool viewerRunning()
    {
        return QDBusConnection::sessionBus().interface()->isServiceRegistered(QString(KDEGlance::DBusService));
    }
    void callViewer(const QString &method, const QVariantList &args = {})
    {
        QDBusInterface iface(QString(KDEGlance::DBusService), QString(KDEGlance::DBusViewerPath), QString(KDEGlance::DBusViewerInterface));
        iface.callWithArgumentList(QDBus::Block, method, args);
    }
    QJsonObject bridgeState()
    {
        QString state;
        QMetaObject::invokeMethod(m_hookBridge(), "State", Qt::DirectConnection, Q_RETURN_ARG(QString, state));
        return QJsonDocument::fromJson(state.toUtf8()).object();
    }
    QObject *m_hookBridge()
    {
        return qApp->property(KDEGlance::BridgeAppProperty).value<QObject *>();
    }
    bool previewShows(const QString &name)
    {
        const bool ok = waitFor([&] {
            if (!viewerRunning()) {
                return false;
            }
            const QJsonObject s = viewer();
            return s.value(u"visible"_s).toBool() && s.value(u"url"_s).toString() == url(name).toString();
        }, 15000);
        if (!ok) {
            qWarning().noquote() << "expected" << name << "viewer:" << QJsonDocument(viewer()).toJson(QJsonDocument::Compact)
                                 << "bridge:" << QJsonDocument(bridgeState()).toJson(QJsonDocument::Compact);
        }
        return ok;
    }
    bool previewClosed()
    {
        return waitFor([&] {
            return !viewer().value(u"visible"_s).toBool();
        });
    }
    void closePreview()
    {
        callViewer(u"Close"_s);
        QVERIFY(previewClosed());
        waitForBridgeIdle();
    }
    void waitForBridgeIdle()
    {
        // The Closed signal reaches the bridge asynchronously.
        QVERIFY(waitFor([&] {
            return bridgeState().value(u"session"_s).toString().isEmpty();
        }));
    }
    void focusView(DolphinView *view)
    {
        m_window->activateWindow();
        view->container()->setFocus();
        QVERIFY(waitFor([&] {
            return QApplication::focusWidget() == view->container();
        }));
    }
    void pressSpace(DolphinView *view)
    {
        focusView(view);
        QTest::keyClick(view->container(), Qt::Key_Space);
    }

private Q_SLOTS:
    void initTestCase()
    {
        // Must look like Dolphin before the hook is loaded.
        QCoreApplication::setApplicationName(u"dolphin"_s);
        QVERIFY(QDBusConnection::sessionBus().isConnected());

        for (const QString &n : {u"z1.png"_s, u"y2.png"_s, u"x3.png"_s, u"w4.png"_s, u"v5.png"_s}) {
            QVERIFY(twoColorImage(300, 200).save(path(n)));
        }
        QDir(m_dir.path()).mkdir(u"folder"_s);

        QPluginLoader loader(QStringLiteral(KDEGLANCE_HOOK_PLUGIN));
        m_hook = loader.instance();
        QVERIFY2(m_hook, qPrintable(loader.errorString()));
        QVERIFY(qobject_cast<KOverlayIconPlugin *>(m_hook));
        QVERIFY(m_hookBridge());

        m_window = std::make_unique<DolphinMainWindow>(2);
        m_view = m_window->view(0);
        // Dolphin's order (folders first, then descending by name here):
        // the preview must follow it, not the alphabet.
        const KFileItemList items{fileItem(u"folder"_s, true), fileItem(u"z1.png"_s), fileItem(u"y2.png"_s), fileItem(u"x3.png"_s), fileItem(u"w4.png"_s)};
        m_view->model()->setItems(items);
        m_window->view(1)->model()->setItems(items);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    }

    void cleanupTestCase()
    {
        if (viewerRunning()) {
            callViewer(u"Quit"_s);
        }
    }

    void hookFoundDolphinInternals()
    {
        const QJsonObject s = bridgeState();
        QVERIFY(s.value(u"modelAccess"_s).toBool());
        QVERIFY(s.value(u"selectionAccess"_s).toBool());
        QCOMPARE(s.value(u"hotkey"_s).toString(), u"Space"_s);
    }

    void spaceOpensSelectedFileViaActivation()
    {
        // The service is not running: D-Bus activation starts it.
        m_view->selectOnly(2); // y2.png
        m_view->container()->keysReceived.clear();
        pressSpace(m_view);
        QVERIFY(previewShows(u"y2.png"_s));
        QVERIFY(!m_view->container()->keysReceived.contains(Qt::Key_Space)); // Dolphin never saw it
        const QJsonObject s = viewer();
        QCOMPARE(s.value(u"count"_s).toInt(), 4); // files only, no folder
        QCOMPARE(s.value(u"index"_s).toInt(), 1); // z1, [y2], x3, w4
    }

    void navigationSyncsSelectionInDolphinOrder()
    {
        KItemListSelectionManager *sel = m_view->container()->selectionManager();
        callViewer(u"Navigate"_s, {1});
        QVERIFY(previewShows(u"x3.png"_s));
        QVERIFY(waitFor([&] {
            return sel->currentItem() == 3 && sel->selection() == QSet<int>{3};
        }));
        QCOMPARE(sel->anchor(), 3);
        QCOMPARE(m_view->container()->listView()->lastScrolled, 3);
        callViewer(u"Navigate"_s, {1});
        QVERIFY(previewShows(u"w4.png"_s));
        QVERIFY(waitFor([&] {
            return sel->currentItem() == 4;
        }));
        // The other (split) view is not touched.
        QCOMPARE(m_window->view(1)->container()->selectionManager()->changes, 0);
    }

    void closingKeepsSelectionAndFocus()
    {
        closePreview();
        KItemListSelectionManager *sel = m_view->container()->selectionManager();
        QCOMPARE(sel->selection(), QSet<int>{4});
        QVERIFY(waitFor([&] {
            return m_view->container()->hasFocus();
        }));
    }

    void spaceAgainTogglesClosed()
    {
        pressSpace(m_view);
        QVERIFY(previewShows(u"w4.png"_s));
        pressSpace(m_view); // e.g. if the compositor kept the focus on Dolphin
        QVERIFY(previewClosed());
        waitForBridgeIdle();
    }

    void previewFollowsDolphinSelection()
    {
        // When Dolphin keeps the keyboard focus, the preview follows its
        // selection (and our own selection changes cause no loop).
        m_view->selectOnly(1);
        pressSpace(m_view);
        QVERIFY(previewShows(u"z1.png"_s));
        m_view->selectOnly(3);
        QVERIFY(previewShows(u"x3.png"_s));
        QCOMPARE(viewer().value(u"count"_s).toInt(), 4);
        callViewer(u"Navigate"_s, {1});
        QVERIFY(previewShows(u"w4.png"_s));
        QVERIFY(waitFor([&] {
            return m_view->container()->selectionManager()->currentItem() == 4;
        }));
        QTest::qWait(200);
        QVERIFY(previewShows(u"w4.png"_s)); // stable, no ping-pong
        m_view->selectOnly(0); // a folder: ignored
        QTest::qWait(200);
        QVERIFY(previewShows(u"w4.png"_s));
        closePreview();
    }

    void folderKeepsDolphinBehaviour()
    {
        m_view->selectOnly(0);
        m_view->container()->keysReceived.clear();
        pressSpace(m_view);
        QTest::qWait(400);
        QVERIFY(m_view->container()->keysReceived.contains(Qt::Key_Space));
        QVERIFY(!viewer().value(u"visible"_s).toBool());
    }

    void typeAheadAndSelectionModeKeepSpace()
    {
        m_view->selectOnly(1);
        KItemListController *c = m_view->container()->controller();
        for (bool *flag : {&c->typeAheadActive, &c->selectionModeActive}) {
            *flag = true;
            m_view->container()->keysReceived.clear();
            pressSpace(m_view);
            QTest::qWait(300);
            QVERIFY(m_view->container()->keysReceived.contains(Qt::Key_Space));
            QVERIFY(!viewer().value(u"visible"_s).toBool());
            *flag = false;
        }
    }

    void textInputKeepsSpace()
    {
        auto *editor = new QTextEdit(m_view->container()->viewport()); // like the inline rename editor
        editor->show();
        editor->setFocus();
        QVERIFY(waitFor([&] {
            return QApplication::focusWidget() == editor;
        }));
        QTest::keyClick(editor, Qt::Key_Space);
        QCOMPARE(editor->toPlainText(), u" "_s);
        QTest::qWait(200);
        QVERIFY(!viewer().value(u"visible"_s).toBool());
        delete editor;
    }

    void modelChangesUpdateTheList()
    {
        m_view->selectOnly(1);
        pressSpace(m_view);
        QVERIFY(previewShows(u"z1.png"_s));
        m_view->model()->insertItem(5, fileItem(u"v5.png"_s));
        QVERIFY(waitFor([&] {
            return viewer().value(u"count"_s).toInt() == 5;
        }));
        m_view->model()->removeItem(5);
        QVERIFY(waitFor([&] {
            return viewer().value(u"count"_s).toInt() == 4;
        }));
        closePreview();
    }

    void syncCanBeDisabled()
    {
        callViewer(u"SetOption"_s, {u"syncSelection"_s, QVariant::fromValue(QDBusVariant(false))});
        QVERIFY(waitFor([&] {
            return !bridgeState().value(u"syncSelection"_s).toBool();
        }));
        m_view->selectOnly(1);
        pressSpace(m_view);
        QVERIFY(previewShows(u"z1.png"_s));
        callViewer(u"Navigate"_s, {1});
        QVERIFY(previewShows(u"y2.png"_s));
        QTest::qWait(200);
        QCOMPARE(m_view->container()->selectionManager()->selection(), QSet<int>{1});
        closePreview();
        callViewer(u"SetOption"_s, {u"syncSelection"_s, QVariant::fromValue(QDBusVariant(true))});
        QVERIFY(waitFor([&] {
            return bridgeState().value(u"syncSelection"_s).toBool();
        }));
    }

    void contextMenuSharesTheMechanism()
    {
        const KPluginMetaData md(QStringLiteral(KDEGLANCE_MENU_PLUGIN));
        QVERIFY(md.isValid());
        auto result = KPluginFactory::instantiatePlugin<KAbstractFileItemActionPlugin>(md, this);
        QVERIFY2(result.plugin, qPrintable(result.errorText));
        // Folders get no entry.
        QVERIFY(result.plugin->actions(KFileItemListProperties(KFileItemList{fileItem(u"folder"_s, true)}), m_window.get()).isEmpty());
        const QList<QAction *> actions = result.plugin->actions(KFileItemListProperties(KFileItemList{fileItem(u"x3.png"_s)}), m_window.get());
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions.first()->text(), u"KDEGlance: Quick Look"_s);
        m_view->selectOnly(1);
        focusView(m_view);
        actions.first()->trigger();
        QVERIFY(previewShows(u"x3.png"_s));
        QCOMPARE(viewer().value(u"count"_s).toInt(), 4); // Dolphin's list, same as Space
        callViewer(u"Navigate"_s, {-1});
        QVERIFY(previewShows(u"y2.png"_s));
        QVERIFY(waitFor([&] {
            return m_view->container()->selectionManager()->currentItem() == 2; // synced
        }));
        closePreview();
    }

    void customHotkey()
    {
        callViewer(u"SetOption"_s, {u"shortcut"_s, QVariant::fromValue(QDBusVariant(u"Ctrl+Y"_s))});
        QVERIFY(waitFor([&] {
            return bridgeState().value(u"hotkey"_s).toString() == u"Ctrl+Y"_s;
        }));
        m_view->selectOnly(1);
        m_view->container()->keysReceived.clear();
        pressSpace(m_view);
        QTest::qWait(300);
        QVERIFY(m_view->container()->keysReceived.contains(Qt::Key_Space));
        QTest::keyClick(m_view->container(), Qt::Key_Y, Qt::ControlModifier);
        QVERIFY(previewShows(u"z1.png"_s));
        closePreview();
        callViewer(u"SetOption"_s, {u"shortcut"_s, QVariant::fromValue(QDBusVariant(u"Space"_s))});
        QVERIFY(waitFor([&] {
            return bridgeState().value(u"hotkey"_s).toString() == u"Space"_s;
        }));
    }

    void longSessionStability()
    {
        for (int i = 0; i < 30; ++i) {
            m_view->selectOnly(1 + i % 4);
            pressSpace(m_view);
            const QString expected = m_view->model()->fileItem(1 + i % 4).url().fileName();
            QVERIFY2(previewShows(expected), qPrintable(u"cycle %1"_s.arg(i)));
            closePreview();
        }
        QCOMPARE(bridgeState().value(u"hotkey"_s).toString(), u"Space"_s);
    }

    void viewerCrashIsRecovered()
    {
        QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(QString(KDEGlance::DBusService));
        QVERIFY(pid.isValid());
        ::kill(pid.value(), SIGKILL);
        QVERIFY(waitFor([&] {
            return !viewerRunning();
        }));
        m_view->selectOnly(3);
        pressSpace(m_view); // Dolphin is unaffected and activation restarts the viewer
        QVERIFY(previewShows(u"x3.png"_s));
        closePreview();
    }
};

QTEST_MAIN(DolphinIntegrationTest)
#include "dolphinintegrationtest.moc"
