#include "app_fonts.h"
#include <QCoreApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExtensionPlugin>
#include <QDir>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>

Q_IMPORT_QML_PLUGIN(ChunfePlugin)

class Setup : public QObject {
    Q_OBJECT
public:
    Setup() {
        if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
            qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    }
public slots:
    void applicationAvailable() {
        QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7"));
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        ChunfeFonts::initialize();
    }
    void qmlEngineAvailable(QQmlEngine *engine) {
        const auto directory = qEnvironmentVariable("CHUNFE_TEST_ARTIFACT_DIR");
        if (!directory.isEmpty())
            QDir().mkpath(directory);
        engine->rootContext()->setContextProperty(QStringLiteral("testArtifactDirectory"), directory);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(native_ui, Setup)
#include "native_ui_tests.moc"
