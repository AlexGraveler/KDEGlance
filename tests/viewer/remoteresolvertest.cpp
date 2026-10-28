// SPDX-License-Identifier: GPL-3.0-or-later
#include "remoteresolver.h"
#include "../testutils.h"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace KDEGlance;
using namespace KDEGlanceTest;
using namespace Qt::StringLiterals;
class FakeFuse : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KIOFuse.VFS")
public:
    QString path;
    bool delay = false;
public Q_SLOTS:
    QString mountUrl(const QString&)
    {
        if (delay) {
            setDelayedReply(true);
            const auto reply = message().createReply(path);
            const auto bus = connection();
            QTimer::singleShot(150, this, [reply, bus] { bus.send(reply); });
            return { };
        }
        return path;
    }
};
class RemoteResolverTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    FakeFuse fuse;
private Q_SLOTS:
    void initTestCase()
    {
        auto bus = QDBusConnection::connectToBus(qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"), u"fake-fuse-connection"_s);
        QVERIFY(bus.registerService(u"org.kde.KIOFuse"_s));
        QVERIFY(bus.registerObject(u"/org/kde/KIOFuse"_s, &fuse, QDBusConnection::ExportAllSlots));
        QFile file(dir.filePath(u"mounted.txt"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("mounted");
    }
    void init()
    {
        fuse.path.clear();
        fuse.delay = false;
    }
    void fuseRandomAccess()
    {
        fuse.path = dir.filePath(u"mounted.txt"_s);
        RemoteResolver r;
        QSignalSpy ready(&r, &RemoteResolver::resolved);
        r.resolve(QUrl(u"smb://server/share/test.txt"_s));
        QVERIFY(waitFor([&] { return ready.size() == 1; }));
        QCOMPARE(ready.first().first().toString(), fuse.path);
        r.cancel();
        QVERIFY(QFile::exists(fuse.path));
    }
    void canceledMountCannotReopen()
    {
        fuse.path = dir.filePath(u"mounted.txt"_s);
        fuse.delay = true;
        RemoteResolver r;
        QSignalSpy ready(&r, &RemoteResolver::resolved);
        r.resolve(QUrl(u"smb://server/share/test.txt"_s));
        QTest::qWait(30);
        r.cancel();
        QTest::qWait(250);
        QVERIFY(ready.isEmpty());
    }
    void kioCopyAndCleanup()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello");
                socket->disconnectFromHost();
            });
        });
        RemoteResolver r;
        QSignalSpy ready(&r, &RemoteResolver::resolved), failed(&r, &RemoteResolver::failed);
        r.resolve(QUrl(u"http://127.0.0.1:%1/file.txt"_s.arg(server.serverPort())));
        QVERIFY2(waitFor([&] { return !ready.isEmpty() || !failed.isEmpty(); }, 15000), "KIO transfer timed out");
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().first().toString()));
        const QString path = ready.first().first().toString();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("hello"));
        file.close();
        r.cancel();
        QVERIFY(!QFile::exists(path));
    }
    void kioAccessError()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        });
        RemoteResolver r;
        QSignalSpy failed(&r, &RemoteResolver::failed);
        r.resolve(QUrl(u"http://127.0.0.1:%1/missing.txt"_s.arg(server.serverPort())));
        QVERIFY(waitFor([&] { return !failed.isEmpty(); }, 15000));
    }
};
QTEST_MAIN(RemoteResolverTest)
#include "remoteresolvertest.moc"
