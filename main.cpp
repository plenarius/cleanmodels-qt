#include "mainwindow.h"
#include <QApplication>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("Clean Models Community");
    QCoreApplication::setApplicationName("Clean Models::EE QT");
    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    MainWindow w;
    w.show();

    return QApplication::exec();
}
