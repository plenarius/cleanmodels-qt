#include "constants.h"
#include "mainwindow.h"
#include <QApplication>
#include <QCommandLineParser>
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

    QCommandLineParser parser;
    parser.setApplicationDescription("Desktop GUI for cleanmodels, the NWN:EE model tool");
    parser.addHelpOption();
    QCommandLineOption variantsOpt("variants",
        "Open Make Variants on <model>, a robe, cloak or armour part.", "model");
    parser.addOption(variantsOpt);
    parser.process(a);

    MainWindow w;
    w.show();
    if (parser.isSet(variantsOpt))
        w.showVariantsDialog(parser.value(variantsOpt));

    return QApplication::exec();
}
