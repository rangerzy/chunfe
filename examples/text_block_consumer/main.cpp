#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTimer>
#ifdef TEXT_BLOCK_PROJECT_FONTS
#include "app_fonts.h"
#endif

Q_IMPORT_QML_PLUGIN(TextBlockPlugin)

int main(int argc, char *argv[])
{
    // Match the experiment app's Qt 6.12 glyph antialiasing policy.
    // This belongs to the consumer, not the reusable module.
    if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
        qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    QGuiApplication app(argc, argv);
#ifdef TEXT_BLOCK_PROJECT_FONTS
    if (!ChunfeFonts::initialize())
        return 1;
#endif
    QCoreApplication::setApplicationName("TextBlock Consumer");

    QCommandLineParser parser;
    parser.setApplicationDescription("Independent TextBlock QML module example");
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(
        "screenshot", "Save the example window as a PNG and exit.", "path");
    parser.addOption(screenshotOption);
    parser.process(app);

    QQmlApplicationEngine engine;
#ifdef TEXT_BLOCK_PROJECT_FONTS
    engine.setInitialProperties({{QStringLiteral("projectFonts"), true},
                                 {QStringLiteral("fontLicenseText"), ChunfeFonts::licenseNotices()}});
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TextBlockConsumer", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(screenshotOption)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (!window)
            return 1;
        const QString path = parser.value(screenshotOption);
        QTimer::singleShot(500, &app, [window, path] {
            const QImage image = window->grabWindow();
            if (image.isNull() || !image.save(path, "PNG")) {
                qWarning("Could not save the example screenshot");
                QCoreApplication::exit(1);
                return;
            }
            QCoreApplication::exit(0);
        });
    }

    return app.exec();
}
