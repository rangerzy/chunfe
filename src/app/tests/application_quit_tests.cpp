#include "../chunfe_application.h"

#include <QtCore/QDebug>
#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QWindow>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickControls2/QQuickStyle>

Q_IMPORT_QML_PLUGIN(ChunfePlugin)

int main(int argc, char **argv)
{
    ChunfeApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    engine.loadFromModule("Chunfe", "Main");
    const auto roots = engine.rootObjects();
    auto *window = roots.isEmpty() ? nullptr : qobject_cast<QWindow *>(roots.first());
    if (!window)
        return 1;
    app.setMainWindow(window);

    auto *demo = window->property("demoController").value<QObject *>();
    const QString state = app.arguments().value(1);
    if (!demo || (state != "idle" && state != "active" && state != "finished"))
        return 1;

    if (state == "idle") {
        if (!QMetaObject::invokeMethod(demo, "startChecks") || !demo->property("preflightVisible").toBool())
            return 1;
    } else {
        if (!QMetaObject::invokeMethod(demo, "beginInterview") || !window->property("transcriptVisible").toBool()
            || !window->property("answerVisible").toBool())
            return 1;
        if (state == "finished" && (!QMetaObject::invokeMethod(demo, "finishInterview") || !window->isVisible()))
            return 1;
    }

    QTimer::singleShot(0, &app, [&] {
        bool mainWindowReopened = false;
        const auto connection = QObject::connect(window, &QWindow::visibleChanged, &app,
            [&](bool visible) { mainWindowReopened |= visible; });

        // Cocoa forwards a native application termination request as QEvent::Quit.
        QEvent quit(QEvent::Quit);
        QCoreApplication::sendEvent(&app, &quit);
        QObject::disconnect(connection);

        if (!quit.isAccepted() || demo->property("interviewActive").toBool() || demo->property("preflightVisible").toBool()
            || mainWindowReopened || window->isVisible() || window->property("transcriptVisible").toBool()
            || window->property("answerVisible").toBool()) {
            qCritical() << "Application quit did not finish cleanup and close its windows" << state;
            app.exit(1);
        }
    });

    return app.exec();
}
