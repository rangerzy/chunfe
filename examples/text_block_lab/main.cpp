#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "app_fonts.h"

int main(int argc, char *argv[])
{
    // Keep newly created and recycled QtRendering glyph nodes visually comparable.
    // Qt 6.12 resets recycled distance-field nodes to grayscale antialiasing.
    // This is an explicit application policy; TextBlock never changes global state.
    if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
        qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    QGuiApplication app(argc, argv);
    if (!ChunfeFonts::initialize())
        return 1;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("fontLicenseText"), ChunfeFonts::licenseNotices()}});

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("TextBlockLab", "Main");

    return app.exec();
}
