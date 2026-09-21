#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    application.setStyleSheet(applicationStyleSheet());
    MainWindow window;
    window.show();
    return application.exec();
}
