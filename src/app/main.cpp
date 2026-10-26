#include "app_fonts.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>

// Q_IMPORT_QML_PLUGIN(ChunfePlugin)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Chunfe"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CHUNFE_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("春风"));

    if (!ChunfeFonts::initialize())
        return 1;

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("fontLicenseText"), ChunfeFonts::licenseNotices()}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Chunfe", "Main");

    return app.exec();
}
