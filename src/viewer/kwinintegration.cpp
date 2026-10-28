// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors
#include "kwinintegration.h"

#include "constants.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcKWin, "kdeglance.kwin", QtInfoMsg)

namespace KDEGlance
{

namespace
{
const QString KWinService = u"org.kde.KWin"_s;
const QString ScriptingPath = u"/Scripting"_s;
const QString ScriptingInterface = u"org.kde.kwin.Scripting"_s;
const QString ScriptInterface = u"org.kde.kwin.Script"_s;
} // namespace

KWinIntegration::KWinIntegration(QObject *parent)
    : QObject(parent)
    , m_scriptPath(locateScript())
{
}

KWinIntegration::~KWinIntegration() = default;

QString KWinIntegration::locateScript()
{
    const QString env = qEnvironmentVariable("KDEGLANCE_KWIN_SCRIPT");
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return env;
    }
    const QString installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, u"kdeglance/kwin/placement.js"_s);
    if (!installed.isEmpty()) {
        return installed;
    }
    // Running from a build tree.
    const QString dev = QCoreApplication::applicationDirPath() + u"/../share/kdeglance/kwin/placement.js"_s;
    return QFileInfo::exists(dev) ? QFileInfo(dev).canonicalFilePath() : QString();
}

bool KWinIntegration::isScriptLoaded() const
{
    return m_loaded;
}

void KWinIntegration::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    if (m_scriptPath.isEmpty()) {
        qCWarning(lcKWin) << "placement script not found; using the compositor's placement";
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    m_watcher = new QDBusServiceWatcher(KWinService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &newOwner) {
        m_loaded = false;
        Q_EMIT scriptStateChanged(false);
        if (!newOwner.isEmpty()) {
            load(); // KWin restarted
        }
    });
    if (bus.interface() && bus.interface()->isServiceRegistered(KWinService)) {
        load();
    }
}

void KWinIntegration::load()
{
    // A previous instance that crashed may have left its script loaded.
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage check = QDBusMessage::createMethodCall(KWinService, ScriptingPath, ScriptingInterface, u"isScriptLoaded"_s);
    check << QString(KWinScriptName);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(check, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<bool> reply = *w;
        if (!reply.isError() && reply.value()) {
            QDBusMessage unload = QDBusMessage::createMethodCall(KWinService, ScriptingPath, ScriptingInterface, u"unloadScript"_s);
            unload << QString(KWinScriptName);
            auto *uw = new QDBusPendingCallWatcher(bus.asyncCall(unload, 3000), this);
            connect(uw, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w2) {
                w2->deleteLater();
                loadNow();
            });
            return;
        }
        loadNow();
    });
}

void KWinIntegration::loadNow()
{
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(KWinService, ScriptingPath, ScriptingInterface, u"loadScript"_s);
    msg << m_scriptPath << QString(KWinScriptName);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        QDBusPendingReply<int> reply = *w;
        if (reply.isError() || reply.value() < 0) {
            qCWarning(lcKWin) << "loading the placement script failed:" << reply.error().message();
            return;
        }
        run(reply.value());
    });
}

void KWinIntegration::run(int id, int attempt)
{
    // Plasma 6 exposes scripts at /Scripting/Script<N>; older KWin at /<N>.
    const QStringList paths{u"/Scripting/Script%1"_s.arg(id), u"/%1"_s.arg(id)};
    if (attempt >= paths.size()) {
        qCWarning(lcKWin) << "could not start the placement script";
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(KWinService, paths.at(attempt), ScriptInterface, u"run"_s);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, id, attempt](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            run(id, attempt + 1);
            return;
        }
        m_loaded = true;
        qCInfo(lcKWin) << "placement script active";
        Q_EMIT scriptStateChanged(true);
    });
}

void KWinIntegration::stop()
{
    if (!m_loaded) {
        return;
    }
    m_loaded = false;
    QDBusMessage msg = QDBusMessage::createMethodCall(KWinService, ScriptingPath, ScriptingInterface, u"unloadScript"_s);
    msg << QString(KWinScriptName);
    QDBusConnection::sessionBus().call(msg, QDBus::Block, 500);
}

} // namespace KDEGlance
