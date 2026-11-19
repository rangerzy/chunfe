#include "app_fonts.h"
#include "app_settings.h"
#include "audio_device_list.h"
#include "audio_level_test.h"
#include "background_analysis_controller.h"
#include "background_controller.h"
#include "chunfe_application.h"
#include "conversation_advice_controller.h"
#include "conversation_store.h"
#include "model_connection_controller.h"
#include "model_test_chat_controller.h"
#include "offline_transcription_controller.h"
#include "cf_log.h"

#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickStyle>
#include <QSettings>
#include <audio_pipline_manager.h>
// Q_IMPORT_QML_PLUGIN(ChunfePlugin)

int main(int argc, char *argv[])
{
    CfLog::initialize();
    // Match the TextBlock lab: use consistent grayscale antialiasing for both
    // newly created and recycled Qt Quick glyph nodes, respecting user settings.
    if (!qEnvironmentVariableIsSet("QSG_DISTANCEFIELD_ANTIALIASING"))
        qputenv("QSG_DISTANCEFIELD_ANTIALIASING", "gray");
    ChunfeApplication app(argc, argv);
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

    const QString configFilePath = AppSettings::configFilePath();
    const QString dataDirectory = AppSettings::dataDirectory();
    if (!configFilePath.isEmpty()) {
        QSettings legacy;
        QSettings config(configFilePath, QSettings::IniFormat);
        if (!AppSettings::migrateLegacyPreferences(legacy, config))
            qWarning("Could not finish migrating Chunfe user settings.");
    }

    AudioPiplineManager audioManager;
    AudioDeviceList audioDevices(audioManager);
    AudioLevelTest audioLevelTest(audioManager);
    OfflineTranscriptionController offlineTranscription(audioManager, configFilePath);
    ModelConnectionController modelConnection(configFilePath);
    ConversationAdviceController conversationAdvice(configFilePath, modelConnection);
    ModelTestChatController modelTestChat(modelConnection);
    BackgroundController backgroundDocument(dataDirectory);
    BackgroundAnalysisController backgroundAnalysis(configFilePath, backgroundDocument, modelConnection);
    ConversationStore conversationStore(dataDirectory);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("audioDevices"), QVariant::fromValue(&audioDevices)},
                                 {QStringLiteral("audioLevelTest"), QVariant::fromValue(&audioLevelTest)},
                                 {QStringLiteral("offlineTranscription"), QVariant::fromValue(&offlineTranscription)},
                                 {QStringLiteral("modelConnection"), QVariant::fromValue(&modelConnection)},
                                 {QStringLiteral("modelTestChat"), QVariant::fromValue(&modelTestChat)},
                                 {QStringLiteral("conversationStore"), QVariant::fromValue(&conversationStore)},
                                 {QStringLiteral("conversationAdvice"), QVariant::fromValue(&conversationAdvice)},
                                 {QStringLiteral("backgroundDocument"), QVariant::fromValue(&backgroundDocument)},
                                 {QStringLiteral("backgroundAnalysis"), QVariant::fromValue(&backgroundAnalysis)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Chunfe", "Main");
    app.setMainWindow(engine.rootObjects().value(0));

    return app.exec();
}
