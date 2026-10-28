// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only adapter injected into a dedicated real Dolphin/viewer process.
// Sends Qt widget events; it does not simulate physical Wayland seat input.
#include <QApplication>
#include <functional>
#include <QDBusConnection>
#include <QContextMenuEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMenu>
#include <QTimer>
#include <QTest>
#include <QWindow>
#include <QWheelEvent>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QAbstractButton>
#include <QComboBox>
#include <QJsonArray>
#include <QSignalSpy>

class SessionAdapter : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeglance.TestAdapter")
    QWidget* targetWindow() const {
        for(auto* w:QApplication::topLevelWidgets())
            if(w->isVisible() && w->objectName()==QStringLiteral("KDEGlancePreview")) return w;
        return QApplication::activeWindow();
    }
public Q_SLOTS:
    Q_SCRIPTABLE void StoreWindowInfo(const QString& data) { setProperty("windowInfo", data); }
    Q_SCRIPTABLE QString WindowInfo() const { return property("windowInfo").toString(); }
    Q_SCRIPTABLE bool FocusFileView() {
        for(auto* top:QApplication::topLevelWidgets()) if(top->inherits("DolphinMainWindow"))
            for(auto* child:top->findChildren<QWidget*>()) if(child->inherits("DolphinView")) {
                child->setFocus(Qt::OtherFocusReason); return true;
            }
        return false;
    }
    Q_SCRIPTABLE QString Tree() {
        QString result;
        std::function<void(QObject *, int)> visit = [&](QObject *o, int depth) {
            result += QString(depth * 2, QLatin1Char(' ')) + QString::fromLatin1(o->metaObject()->className()) + QLatin1Char('\n');
            for (auto *c : o->children()) visit(c, depth + 1);
        };
        for (auto *w : QApplication::topLevelWidgets()) if (w->inherits("DolphinMainWindow")) visit(w, 0);
        return result;
    }
    Q_SCRIPTABLE QString State() {
        auto *w = QApplication::activeWindow();
        auto *f = QApplication::focusWidget();
        return QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("active"), w ? w->windowTitle() : QString()},
            {QStringLiteral("focus"), f ? QString::fromLatin1(f->metaObject()->className()) : QString()}
        }).toJson(QJsonDocument::Compact));
    }
    Q_SCRIPTABLE bool Key(const QString &key) {
        auto *f = QApplication::focusWidget();
        if (auto* w=targetWindow(); w && (!f || f->window()!=w)) f=w;
        if (!f) return false;
        const auto sequence = QKeySequence::fromString(key);
        if (sequence.isEmpty()) return false;
        QTest::keyClick(f, sequence[0].key(), sequence[0].keyboardModifiers());
        return true;
    }
    Q_SCRIPTABLE bool Menu() {
        auto *f = QApplication::focusWidget();
        if (auto* w=targetWindow(); w && (!f || f->window()!=w)) f=w;
        if (!f) return false;
        QContextMenuEvent event(QContextMenuEvent::Keyboard, f->rect().center(), f->mapToGlobal(f->rect().center()));
        QApplication::sendEvent(f, &event);
        return true;
    }
    Q_SCRIPTABLE bool QuickLook() {
        for (auto *widget : QApplication::topLevelWidgets()) {
            if (auto *menu = qobject_cast<QMenu *>(widget); menu && menu->isVisible()) {
                for (auto *action : menu->actions()) {
                    if (action->text().contains(QStringLiteral("KDEGlance"))) {
                        menu->hide();
                        action->trigger();
                        return true;
                    }
                }
            }
        }
        return false;
    }
    Q_SCRIPTABLE QString Inspect() {
        auto* w=targetWindow(); if(!w) return QStringLiteral("{}");
        QJsonArray buttons,frames,combos;
        for(auto* b:w->findChildren<QAbstractButton*>()) if(b->isVisible()) buttons.append(b->objectName()+QStringLiteral(":")+b->toolTip());
        for(auto* a:w->findChildren<QAbstractScrollArea*>()) if(a->isVisible()) frames.append(int(a->frameShape()));
        for(auto* c:w->findChildren<QComboBox*>()) if(c->isVisible()) combos.append(c->objectName());
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"frameless",bool(w->windowFlags() & Qt::FramelessWindowHint)},
            {"buttons",buttons},{"frames",frames},{"combos",combos}}).toJson(QJsonDocument::Compact));
    }
    Q_SCRIPTABLE int Drag(const QString& name,bool alt) {
        auto* w=targetWindow(); auto* target=w?w->findChild<QWidget*>(name):nullptr;
        if(auto* area=qobject_cast<QAbstractScrollArea*>(target)) target=area->viewport();
        if(!target)return -1;
        QObject* view=target;
        while(view && view->metaObject()->indexOfSignal("systemMoveRequested()")<0) view=view->parent();
        if(!view)return -1;
        QSignalSpy spy(view,SIGNAL(systemMoveRequested()));
        const auto mods=alt?Qt::AltModifier:Qt::NoModifier;
        QTest::mousePress(target,Qt::LeftButton,mods,QPoint(80,80));
        QMouseEvent motion(QEvent::MouseMove,QPointF(140,140),target->mapToGlobal(QPoint(140,140)),Qt::NoButton,Qt::LeftButton,mods);
        QApplication::sendEvent(target,&motion);
        QTest::mouseRelease(target,Qt::LeftButton,mods,QPoint(140,140));
        return spy.size();
    }
    Q_SCRIPTABLE bool Click(const QString &name, int x, int y) {
        auto *w=targetWindow();
        auto *target=w ? w->findChild<QWidget *>(name) : nullptr;
        if(!target)return false;
        QTest::mouseClick(target,Qt::LeftButton,{},QPoint(x,y));return true;
    }
    Q_SCRIPTABLE bool Wheel(const QString &name, int x, int y, int delta, bool control) {
        auto *w=targetWindow();auto *target=w?w->findChild<QWidget *>(name):nullptr;
        if(!target)return false;
        if(auto *area=qobject_cast<QAbstractScrollArea *>(target))target=area->viewport();
        const QPointF position(x,y);
        QWheelEvent event(position,target->mapToGlobal(position.toPoint()),{},QPoint(0,delta),Qt::NoButton,control?Qt::ControlModifier:Qt::NoModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(target,&event);return event.isAccepted();
    }
    Q_SCRIPTABLE int ScrollPosition(const QString &name) {
        auto *w=targetWindow();auto *area=w?w->findChild<QAbstractScrollArea *>(name):nullptr;
        return area?area->verticalScrollBar()->value():-1;
    }
    Q_SCRIPTABLE bool ResizePreview(int width,int height,bool aspect) {
        auto* w=targetWindow(); if(!w)return false;
        const Qt::Edges edges=Qt::RightEdge|Qt::BottomEdge;
        if(!QMetaObject::invokeMethod(w,"beginInteractiveResize",Qt::DirectConnection,Q_ARG(Qt::Edges,edges),Q_ARG(bool,aspect)))return false;
        w->resize(width,height);
        return QMetaObject::invokeMethod(w,"endInteractiveResize",Qt::DirectConnection);
    }
    Q_SCRIPTABLE bool MiddleDouble(const QString& name,int x,int y) {
        auto* w=targetWindow(); if(!w)return false;
        auto* target=w->findChild<QWidget*>(name); if(!target) target=w;
        QTest::mouseClick(target,Qt::MiddleButton,{},QPoint(x,y));
        QTest::mouseClick(target,Qt::MiddleButton,{},QPoint(x,y));
        return true;
    }
    Q_SCRIPTABLE bool Resize(int width, int height) {
        auto *w = targetWindow();
        if (!w) return false;
        w->resize(width, height);
        return true;
    }
    Q_SCRIPTABLE bool Snapshot(const QString &path) {
        auto *w = targetWindow();
        return w && w->grab().save(path);
    }
    Q_SCRIPTABLE void Quit() { QCoreApplication::quit(); }
};
static void setupAdapter() {
    QTimer::singleShot(0, qApp, [] {
        auto bus = QDBusConnection::sessionBus();
        auto *adapter = new SessionAdapter;
        adapter->setParent(qApp);
        bus.registerObject(QStringLiteral("/KDEGlanceTest"), adapter, QDBusConnection::ExportScriptableSlots);
        bus.registerService(QStringLiteral("org.kde.kdeglance.Test.p%1").arg(QCoreApplication::applicationPid()));
    });
}
Q_COREAPP_STARTUP_FUNCTION(setupAdapter)
#include "sessionadapter.moc"
