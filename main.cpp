#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("Clean Models Community");
    QCoreApplication::setApplicationName("Clean Models::EE QT");
    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    MainWindow w;
    w.show();

    return QApplication::exec();
}
