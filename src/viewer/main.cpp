// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

#include "config-kdeglance.h"
#include "constants.h"
#include "formatprobe.h"
#include "mediabackend.h"
#include "l10n.h"
#include "settings.h"
#include "viewerapp.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QIcon>
#include <QImageReader>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QTextStream>
#include <QUrl>

#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

using namespace Qt::StringLiterals;
using namespace KDEGlance;

namespace
{
QStringList toUrlStrings(const QStringList &args)
{
    QStringList urls;
    for (const QString &a : args) {
        urls << QUrl::fromUserInput(a, QDir::currentPath(), QUrl::AssumeLocalFile).toString();
    }
    return urls;
}

// Calls the running (or D-Bus-activatable) service. Returns false if there is
// no service to talk to.
bool callService(const QString &method, const QVariantList &args, QDBusMessage *replyOut = nullptr)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBusService), QString(DBusViewerPath), QString(DBusViewerInterface), method);
    msg.setArguments(args);
    const QDBusMessage reply = bus.call(msg, QDBus::Block, 8000);
    if (replyOut) {
        *replyOut = reply;
    }
    return reply.type() == QDBusMessage::ReplyMessage;
}

int g_signalPipe[2] = {-1, -1};

void handleTerminationSignals()
{
    // systemd stops the service with SIGTERM: quit cleanly (unload the KWin
    // script, save settings). The handler only writes to a pipe, which is
    // async-signal-safe; the event loop does the rest.
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_signalPipe) != 0) {
        return;
    }
    auto *notifier = new QSocketNotifier(g_signalPipe[1], QSocketNotifier::Read, qApp);
    QObject::connect(notifier, &QSocketNotifier::activated, qApp, [notifier] {
        char c;
        [[maybe_unused]] auto n = ::read(g_signalPipe[1], &c, 1);
        notifier->setEnabled(false);
        QCoreApplication::quit();
    });
    struct sigaction sa {};
    sa.sa_handler = [](int) {
        const char c = 1;
        [[maybe_unused]] auto n = ::write(g_signalPipe[0], &c, 1);
    };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);
}
} // namespace

int main(int argc, char **argv)
{
    // Images up to 2 GiB decoded size (Qt's default 256 MiB rejects large
    // panoramas and medium-format scans).
    QImageReader::setAllocationLimit(2048);

    // Hardware decoding preference must be in the environment before the
    // first media player is created.
    {
        Settings early;
        early.load();
        if (!early.hardwareDecoding() && qEnvironmentVariableIsEmpty("QT_FFMPEG_DECODING_HW_DEVICE_TYPES")) {
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(u"kdeglance"_s);
    QApplication::setApplicationDisplayName(u"KDEGlance"_s);
    QApplication::setApplicationVersion(QStringLiteral(KDEGLANCE_VERSION));
    QApplication::setOrganizationDomain(u"kde.org"_s);
    QApplication::setDesktopFileName(QString(AppId)); // Wayland app id
    QApplication::setWindowIcon(QIcon::fromTheme(QString(AppId), QIcon::fromTheme(u"document-preview"_s)));
    QApplication::setQuitOnLastWindowClosed(false);

    // Qt's own strings (standard dialog buttons) in the user's language.
    auto *qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(QLocale(uiLanguage()), u"qtbase"_s, u"_"_s, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(qtTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(tr_("Quick Look style file previews for Dolphin"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption serviceOption(u"service"_s, tr_("Run as the background preview service."));
    const QCommandLineOption settingsOption(u"settings"_s, tr_("Show the settings window."));
    const QCommandLineOption resetOption(u"reset-settings"_s, tr_("Reset KDEGlance settings to defaults."));
    const QCommandLineOption quitOption(u"quit"_s, tr_("Stop the running service."));
    const QCommandLineOption statusOption(u"status"_s, tr_("Print the state of the running service (JSON)."));
    const QCommandLineOption probeOption(u"probe-formats"_s, tr_("Decode every file in DIR and report which formats work."), u"DIR"_s);
    const QCommandLineOption jsonOption(u"json"_s, tr_("JSON output for --probe-formats."));
    parser.addOptions({serviceOption, settingsOption, resetOption, quitOption, statusOption, probeOption, jsonOption});
    parser.addPositionalArgument(u"files"_s, tr_("Files to preview."), u"[files...]"_s);
    QCommandLineOption checkOption(u"check-video-decoder"_s);
    checkOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(checkOption);
    parser.process(app);
    if (parser.isSet(checkOption)) {
        return checkVideoDecoder();
    }

    if (parser.isSet(probeOption)) {
        configureMediaBackend();
        return FormatProbe::runCli(parser.value(probeOption), parser.isSet(jsonOption));
    }
    if (parser.isSet(quitOption)) {
        callService(u"Quit"_s, {});
        return 0;
    }
    if (parser.isSet(statusOption)) {
        QDBusMessage reply;
        if (!callService(u"Status"_s, {}, &reply)) {
            QTextStream(stderr) << "KDEGlance service is not running\n";
            return 1;
        }
        QTextStream(stdout) << reply.arguments().value(0).toString() << '\n';
        return 0;
    }
    if (parser.isSet(resetOption)) {
        if (!callService(u"ResetSettings"_s, {})) {
            Settings settings;
            settings.resetToDefaults();
            settings.save();
        }
        return 0;
    }

    const QStringList files = parser.positionalArguments();
    const bool runService = parser.isSet(serviceOption);

    // Client mode: hand the request to the service (D-Bus activation starts
    // it if needed) and exit immediately.
    if (!runService) {
        if (parser.isSet(settingsOption)) {
            if (callService(u"ShowSettings"_s, {})) {
                return 0;
            }
        } else if (!files.isEmpty()) {
            const QVariantMap options{{u"origin"_s, u"cli"_s}};
            if (callService(u"Open"_s, {toUrlStrings(files), 0, options})) {
                return 0;
            }
        } else {
            parser.showHelp(0);
        }
    }

    // Service mode (or no service reachable: become the service ourselves).
    configureMediaBackend();
    ViewerApp viewer;
    if (!viewer.registerOnBus()) {
        if (runService) {
            // Another instance owns the name: nothing to do.
            return 0;
        }
        // No D-Bus at all: still show the files, standalone.
    }
    viewer.startDesktopIntegration();
    handleTerminationSignals();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &viewer, &ViewerApp::shutdown);

    if (parser.isSet(settingsOption)) {
        viewer.ShowSettings();
    } else if (!files.isEmpty()) {
        viewer.Open(toUrlStrings(files), 0, QVariantMap{{u"origin"_s, u"cli"_s}});
    }
    return app.exec();
}
