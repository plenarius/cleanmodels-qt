#include "constants.h"
#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat fmt;
    fmt.setVersion(GLDefaults::MajorVersion, GLDefaults::MinorVersion);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(GLDefaults::DepthBits);
    fmt.setSamples(GLDefaults::Samples);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("Clean Models Community");
    QCoreApplication::setApplicationName("Clean Models::EE QT");
    QCoreApplication::setApplicationVersion(QT_VERSION_STR);
    MainWindow w;
    w.show();

    return QApplication::exec();
}
