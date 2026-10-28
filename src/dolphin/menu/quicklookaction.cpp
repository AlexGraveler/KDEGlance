// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KDEGlance contributors

// "KDEGlance: Quick Look" in Dolphin's context menu. The action does not open
// the viewer itself: it hands the request to the in-process Bridge (see the
// hook plugin), so the menu and the Space key share one code path. Outside
// Dolphin (Konqueror, file dialogs) it falls back to the viewer's D-Bus API.

#include "constants.h"

#include <KAbstractFileItemActionPlugin>
#include <KFileItem>
#include <KFileItemListProperties>
#include <KPluginFactory>

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QIcon>
#include <QPointer>
#include <QWidget>

using namespace Qt::StringLiterals;

using namespace KDEGlance;

// Not in a namespace: K_PLUGIN_CLASS_WITH_JSON derives the factory name
// from the class name.
class KDEGlanceQuickLookAction : public KAbstractFileItemActionPlugin
{
    Q_OBJECT
public:
    KDEGlanceQuickLookAction(QObject *parent, const QVariantList &)
        : KAbstractFileItemActionPlugin(parent)
    {
    }

    QList<QAction *> actions(const KFileItemListProperties &props, QWidget *parentWidget) override
    {
        const KFileItemList items = props.items();
        QList<QUrl> urls;
        for (const KFileItem &item : items) {
            if (!item.isDir()) {
                urls << item.url();
            }
        }
        if (urls.isEmpty()) {
            return {};
        }
        auto *action = new QAction(QIcon::fromTheme(u"org.kde.kdeglance"_s, QIcon::fromTheme(u"document-preview"_s)), u"KDEGlance: Quick Look"_s, parentWidget);
        QPointer<QWidget> window = parentWidget ? parentWidget->window() : nullptr;
        connect(action, &QAction::triggered, this, [urls, window] {
            QObject *bridge = qApp->property(BridgeAppProperty).value<QObject *>();
            bool handled = false;
            if (bridge) {
                QMetaObject::invokeMethod(bridge, "openFromContextMenu", Qt::DirectConnection, Q_RETURN_ARG(bool, handled), Q_ARG(QList<QUrl>, urls),
                                          Q_ARG(QWidget *, window.data()));
            }
            if (!handled) {
                QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBusService), QString(DBusViewerPath), QString(DBusViewerInterface), u"Open"_s);
                msg << QStringList{urls.first().toString()} << 0 << QVariantMap{{u"origin"_s, u"menu"_s}};
                QDBusConnection::sessionBus().asyncCall(msg);
            }
        });
        return {action};
    }
};

K_PLUGIN_CLASS_WITH_JSON(KDEGlanceQuickLookAction, "kdeglance_quicklook.json")

#include "quicklookaction.moc"
