#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "app_fonts.h"
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    ChunfeFonts::initialize();

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
