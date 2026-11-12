#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "cf_log.h"
#ifdef CALENDAR_PROJECT_FONTS
#include "app_fonts.h"
#endif
int main(int argc, char *argv[])
{
    CfLog::initialize();
    QGuiApplication app(argc, argv);
#ifdef CALENDAR_PROJECT_FONTS
    ChunfeFonts::initialize();
#endif

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("calendar_before_today", "Main");

    return QGuiApplication::exec();
}
