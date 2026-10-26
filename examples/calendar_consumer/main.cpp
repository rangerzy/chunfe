#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTimer>
#ifdef CALENDAR_PROJECT_FONTS
#include "app_fonts.h"
#endif

Q_IMPORT_QML_PLUGIN(CalendarPlugin)

int main(int argc, char *argv[])
{
    // Glyph rendering is an application policy, independent of Calendar.
    if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
        qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    QGuiApplication app(argc, argv);
#ifdef CALENDAR_PROJECT_FONTS
    if (!ChunfeFonts::initialize())
        return 1;
#endif
    QCoreApplication::setApplicationName("Calendar Consumer");

    QCommandLineParser parser;
    parser.setApplicationDescription("Independent Calendar QML module example");
    parser.addHelpOption();
    const QCommandLineOption screenshotOption(
        "screenshot", "Save the example window as a PNG and exit.", "path");
    parser.addOption(screenshotOption);
    parser.process(app);

    QQmlApplicationEngine engine;
#ifdef CALENDAR_PROJECT_FONTS
    engine.setInitialProperties({{QStringLiteral("projectFonts"), true},
                                 {QStringLiteral("fontLicenseText"), ChunfeFonts::licenseNotices()}});
#endif
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("CalendarConsumer", "Main");
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
