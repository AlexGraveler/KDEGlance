// SPDX-License-Identifier: GPL-3.0-or-later
// Runs a real application event loop: qExec's nested loops do not expose automatic quit.
#include "previewwindow.h"
#include "viewerapp.h"
#include <QApplication>
#include <QTest>
#include <QTimer>
using namespace KDEGlance;
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("kdeglance-resident-open-probe"));
    QApplication::setQuitOnLastWindowClosed(false);
    if (argc != 2)
        return 3;
    ViewerApp viewer;
    bool pressed = false, completed = false;
    viewer.open({ QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])) }, 0, { }, false);
    QTimer press;
    press.setInterval(20);
    QObject::connect(&press, &QTimer::timeout, &app, [&] {
        if (!viewer.window()->isVisible())
            return;
        press.stop();
        pressed = true;
        QTest::keyClick(viewer.window(), Qt::Key_Return);
    });
    press.start();
    QTimer::singleShot(1500, &app, [&] {
        completed = pressed && !viewer.IsVisible();
        app.quit();
    });
    app.exec();
    return completed ? 0 : 1;
}
