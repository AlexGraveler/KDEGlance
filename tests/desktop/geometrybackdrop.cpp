// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QScreen>
#include <QWidget>

int main(int argc,char**argv)
{
    QApplication app(argc,argv);
    app.setApplicationName(QStringLiteral("kdeglance-geometry-backdrop"));
    app.setDesktopFileName(QStringLiteral("kdeglance-geometry-backdrop"));
    QWidget backdrop(nullptr,Qt::Window|Qt::FramelessWindowHint);
    backdrop.setWindowTitle(QStringLiteral("KDEGlance test backdrop"));
    backdrop.setStyleSheet(QStringLiteral("background:#171d23"));
    backdrop.setGeometry(app.primaryScreen()->geometry());
    backdrop.show();
    return app.exec();
}
