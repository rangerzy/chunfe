#include "app_fonts.h"
#include "audio_device_list.h"
#include "audio_level_test.h"
#include "offline_transcription_controller.h"
#include "cf_log.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickStyle>
#include <audio_pipline_manager.h>
// Q_IMPORT_QML_PLUGIN(ChunfePlugin)

int main(int argc, char *argv[])
{
    CfLog::initialize();
    // Match the TextBlock lab: use consistent grayscale antialiasing for both
    // newly created and recycled Qt Quick glyph nodes, respecting user settings.
    if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
        qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rangerzy"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.rangerzy"));
    QCoreApplication::setApplicationName(QStringLiteral("Chunfe"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CHUNFE_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("春风"));
#ifdef Q_OS_LINUX
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.rangerzy.chunfe"));
#endif
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/chunfe-app.svg")));
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    if (!ChunfeFonts::initialize())
        return 1;
    AudioPiplineManager audioManager;
    AudioDeviceList audioDevices(audioManager);
    AudioLevelTest audioLevelTest(audioManager);
    OfflineTranscriptionController offlineTranscription(audioManager);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("audioDevices"), QVariant::fromValue(&audioDevices)},
                                 {QStringLiteral("audioLevelTest"), QVariant::fromValue(&audioLevelTest)},
                                 {QStringLiteral("offlineTranscription"), QVariant::fromValue(&offlineTranscription)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Chunfe", "Main");

    return app.exec();
}
