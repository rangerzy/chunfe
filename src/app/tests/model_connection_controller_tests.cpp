#include "fake_network.h"
#include "model_connection_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {
using TestNetwork::FakeNetwork;

const QByteArray ModelList = R"({"object":"list","data":[{"id":"another-model","object":"model","owned_by":"deepseek"}]})";
}

class ModelConnectionControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_settings = std::make_unique<QTemporaryDir>();
        QVERIFY(m_settings->isValid());
    }

    void savingFirstKeyAutomaticallyGetsModelList()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        controller.setDraftApiKey(QStringLiteral(" fixture-key "));
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);

        QVERIFY(controller.saveModel());
        QCOMPARE(controller.verificationStatus(), QStringLiteral("testing"));
        QCOMPARE(network.operation, QNetworkAccessManager::GetOperation);
        QCOMPARE(network.request.url(), QUrl(QStringLiteral("https://api.deepseek.com/models")));
        QCOMPARE(network.request.rawHeader("Authorization"), QByteArray("Bearer fixture-key"));
        QCOMPARE(network.request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 int(QNetworkRequest::ManualRedirectPolicy));
        QVERIFY(network.body.isEmpty());
        QVERIFY(network.reply);
        network.reply->complete(200, ModelList);

        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
        QVERIFY(!controller.verificationMessage().isEmpty());
    }

    void manualRetryUsesTheSavedKeyAndDoesNotDuplicateAPendingCheck()
    {
        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("modelConnection/apiKey"), QStringLiteral("fixture-key"));
        settings.sync();
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("untested"));
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);

        controller.setDraftApiKey(QStringLiteral("fixture-unsaved-key"));
        QVERIFY(controller.testConnection());
        QCOMPARE(network.request.rawHeader("Authorization"), QByteArray("Bearer fixture-key"));
        auto *initial = network.reply;
        QVERIFY(initial);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("testing"));
        QVERIFY(!controller.testConnection());
        QCOMPARE(network.reply, initial);
        initial->complete(401, {}, QNetworkReply::AuthenticationRequiredError);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("failed"));

        QVERIFY(controller.testConnection());
        QVERIFY(network.reply && network.reply != initial);
        network.reply->complete(200, R"({"object":"list","data":[]})");
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
        QCOMPARE(controller.savedApiKey(), QStringLiteral("fixture-key"));
    }

    void savingOnlyTheModelDuringAPendingCheckDoesNotRestartIt()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key"));
        QVERIFY(controller.saveModel());
        auto *pending = network.reply;
        QVERIFY(pending);
        QSignalSpy verificationChanges(&controller, &ModelConnectionController::verificationStatusChanged);

        controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        QVERIFY(controller.saveModel());
        QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(controller.verificationStatus(), QStringLiteral("testing"));
        QCOMPARE(network.reply, pending);
        QVERIFY(verificationChanges.isEmpty());
        pending->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
    }

    void modelOnlyAndUnchangedSavesKeepTheLastCheck_data()
    {
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<QString>("verdict");
        QTest::newRow("successful") << 200 << QStringLiteral("passed");
        QTest::newRow("failed") << 401 << QStringLiteral("failed");
    }

    void modelOnlyAndUnchangedSavesKeepTheLastCheck()
    {
        QFETCH(int, httpStatus);
        QFETCH(QString, verdict);

        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key"));
        QVERIFY(controller.saveModel());
        auto *completedReply = network.reply;
        QVERIFY(completedReply);
        completedReply->complete(httpStatus, ModelList);
        QCOMPARE(controller.verificationStatus(), verdict);
        const QString message = controller.verificationMessage();
        QSignalSpy verificationChanges(&controller, &ModelConnectionController::verificationStatusChanged);

        controller.setDraftApiKey(QStringLiteral("  fixture-key  "));
        QVERIFY(controller.saveModel());
        controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        QVERIFY(controller.saveModel());

        QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(controller.verificationStatus(), verdict);
        QCOMPARE(controller.verificationMessage(), message);
        QCOMPARE(network.reply, completedReply);
        QVERIFY(verificationChanges.isEmpty());
    }

    void savingAChangedKeyReplacesThePendingCheckAndIgnoresItsLateReply()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-old-key"));
        QVERIFY(controller.saveModel());
        auto *previous = network.reply;
        QVERIFY(previous);

        controller.setDraftApiKey(QStringLiteral("fixture-next-key"));
        QVERIFY(controller.saveModel());
        QVERIFY(network.reply && network.reply != previous);
        QCOMPARE(network.request.rawHeader("Authorization"), QByteArray("Bearer fixture-next-key"));
        QCOMPARE(previous->error(), QNetworkReply::OperationCanceledError);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("testing"));
        previous->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("testing"));

        network.reply->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
    }

    void restoresSavedModelAndApiKeyAcrossControllerLifetimes()
    {
        const QString key = QStringLiteral("fixture-saved-key");
        {
            FakeNetwork network;
            ModelConnectionController controller(configFilePath(), &network);
            QSignalSpy notifications(&controller, &ModelConnectionController::notification);
            controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
            controller.setDraftApiKey(QStringLiteral(" \t") + key + QStringLiteral("\n"));
            QVERIFY(controller.configurationDirty());
            QVERIFY(!controller.modelConfigured());
            QVERIFY(controller.saveModel());

            QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-v4-pro"));
            QCOMPARE(controller.savedApiKey(), key);
            QVERIFY(!controller.configurationDirty());
            QVERIFY(controller.modelConfigured());
            QVERIFY(notifications.isEmpty());
        }

        QSettings settings(configFilePath(), QSettings::IniFormat);
        QCOMPARE(settings.allKeys().size(), 2);
        QCOMPARE(settings.value(QStringLiteral("modelConnection/modelId")).toString(),
                 QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(settings.value(QStringLiteral("modelConnection/apiKey")).toString(), key);
        QFile persistedFile(settings.fileName());
        QVERIFY(persistedFile.open(QIODevice::ReadOnly));
        QVERIFY(persistedFile.readAll().contains(key.toUtf8()));

        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.savedModelName(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(reopened.draftModelName(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(reopened.savedApiKey(), key);
        QCOMPARE(reopened.draftApiKey(), key);
        QVERIFY(reopened.modelConfigured());
        QVERIFY(!reopened.configurationDirty());
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("untested"));
        QCOMPARE(restoredNetwork.operation, QNetworkAccessManager::UnknownOperation);
    }

    void restoresLegacyModelWhenKeyPreferenceIsAbsent()
    {
        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("modelConnection/modelId"), QStringLiteral("deepseek-v4-pro"));
        settings.sync();

        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.savedModelName(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(reopened.draftModelName(), QStringLiteral("deepseek-v4-pro"));
        QVERIFY(reopened.savedApiKey().isEmpty());
        QVERIFY(reopened.draftApiKey().isEmpty());
    }

    void restoresCompletedVerificationWithoutNetworking()
    {
        {
            FakeNetwork network;
            ModelConnectionController controller(configFilePath(), &network);
            controller.setDraftApiKey(QStringLiteral("fixture-key"));
            QVERIFY(controller.saveModel());
            QVERIFY(network.reply);
            network.reply->complete(200, ModelList);
            QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
            QVERIFY(!controller.verificationRestored());
        }

        FakeNetwork network;
        ModelConnectionController reopened(configFilePath(), &network);
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("passed"));
        QVERIFY(reopened.verificationRestored());
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
        reopened.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        QVERIFY(reopened.saveModel());
        QVERIFY(reopened.verificationRestored());
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("passed"));
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
    }

    void pendingManualCheckDoesNotOverwriteTheLastCompletedVerification()
    {
        {
            FakeNetwork network;
            ModelConnectionController controller(configFilePath(), &network);
            controller.setDraftApiKey(QStringLiteral("fixture-key"));
            QVERIFY(controller.saveModel());
            network.reply->complete(200, ModelList);
        }
        {
            FakeNetwork network;
            ModelConnectionController reopened(configFilePath(), &network);
            QVERIFY(reopened.verificationRestored());
            QVERIFY(reopened.testConnection());
            QCOMPARE(reopened.verificationStatus(), QStringLiteral("testing"));
            QVERIFY(!reopened.verificationRestored());
        }

        FakeNetwork network;
        ModelConnectionController reopened(configFilePath(), &network);
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("passed"));
        QVERIFY(reopened.verificationRestored());
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
    }

    void changingOrClearingTheKeyInvalidatesTheCompletedVerification_data()
    {
        QTest::addColumn<QString>("replacement");
        QTest::addColumn<QString>("freshStatus");
        QTest::newRow("changed") << QStringLiteral("fixture-next-key") << QStringLiteral("untested");
        QTest::newRow("cleared") << QString() << QStringLiteral("unconfigured");
    }

    void changingOrClearingTheKeyInvalidatesTheCompletedVerification()
    {
        QFETCH(QString, replacement);
        QFETCH(QString, freshStatus);

        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key"));
        QVERIFY(controller.saveModel());
        network.reply->complete(200, ModelList);
        QVERIFY(controller.testConnection());
        auto *old = network.reply;
        controller.setDraftApiKey(replacement);
        QVERIFY(controller.saveModel());
        QCOMPARE(old->error(), QNetworkReply::OperationCanceledError);
        old->complete(200, ModelList);

        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.beginGroup(QStringLiteral("modelConnection/verification"));
        QVERIFY(settings.allKeys().isEmpty());
        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.verificationStatus(), freshStatus);
        QVERIFY(!reopened.verificationRestored());
        QCOMPARE(restoredNetwork.operation, QNetworkAccessManager::UnknownOperation);
    }

    void resultIsNotSavedAfterAnExternalKeyChange()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-old-key"));
        QVERIFY(controller.saveModel());

        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("modelConnection/apiKey"), QStringLiteral("fixture-replacement-key"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        QSignalSpy notifications(&controller, &ModelConnectionController::notification);
        network.reply->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
        QVERIFY(!controller.verificationRestored());
        QCOMPARE(notifications.size(), 1);
        QVERIFY(!notifications.at(0).at(0).toString().contains(QStringLiteral("fixture-")));
        QVERIFY(!settings.contains(QStringLiteral("modelConnection/verification/status")));

        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.savedApiKey(), QStringLiteral("fixture-replacement-key"));
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("untested"));
        QVERIFY(!reopened.verificationRestored());
        QCOMPARE(restoredNetwork.operation, QNetworkAccessManager::UnknownOperation);
    }

    void savingAnEmptyKeyRemovesTheRetainedKey()
    {
        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("unrelated/test"), QStringLiteral("preserve"));
        settings.sync();
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key"));
        QVERIFY(controller.saveModel());
        QVERIFY(!controller.savedApiKey().isEmpty());
        QVERIFY(settings.contains(QStringLiteral("modelConnection/apiKey")));
        QVERIFY(controller.modelConfigured());
        auto *pendingReply = network.reply;
        QVERIFY(pendingReply);

        QSignalSpy notifications(&controller, &ModelConnectionController::notification);
        controller.setDraftApiKey(QStringLiteral(" \t\n"));
        controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        QVERIFY(controller.saveModel());

        QVERIFY(controller.savedApiKey().isEmpty());
        QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-v4-pro"));
        QVERIFY(!controller.modelConfigured());
        QCOMPARE(controller.verificationStatus(), QStringLiteral("unconfigured"));
        QCOMPARE(network.reply, pendingReply);
        QCOMPARE(pendingReply->error(), QNetworkReply::OperationCanceledError);
        QSignalSpy verificationChanges(&controller, &ModelConnectionController::verificationStatusChanged);
        pendingReply->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("unconfigured"));
        QVERIFY(verificationChanges.isEmpty());
        QVERIFY(notifications.isEmpty());
        QVERIFY(!settings.contains(QStringLiteral("modelConnection/apiKey")));
        QCOMPARE(settings.value(QStringLiteral("unrelated/test")).toString(), QStringLiteral("preserve"));

        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.draftModelName(), QStringLiteral("deepseek-v4-pro"));
        QVERIFY(reopened.draftApiKey().isEmpty());
    }

    void storageFailurePreservesTheSavedProfile()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-old-key"));
        QVERIFY(controller.saveModel());
        QCOMPARE(controller.savedApiKey(), QStringLiteral("fixture-old-key"));
        QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-flash"));
        auto *savedReply = network.reply;
        QVERIFY(savedReply);
        savedReply->complete(200, ModelList);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
        const QString originalDirectory = QFileInfo(configFilePath()).absolutePath();
        const QString preservedDirectory = m_settings->filePath(QStringLiteral("previous"));
        QVERIFY(QDir().rename(originalDirectory, preservedDirectory));
        QFile blocker(originalDirectory);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        QSignalSpy notifications(&controller, &ModelConnectionController::notification);
        QVERIFY(controller.saveModel());
        QVERIFY(notifications.isEmpty());
        controller.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        controller.setDraftApiKey(QStringLiteral("fixture-new-key"));
        QVERIFY(!controller.saveModel());
        QCOMPARE(network.reply, savedReply);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("passed"));
        QCOMPARE(controller.savedModelName(), QStringLiteral("deepseek-flash"));
        QCOMPARE(controller.savedApiKey(), QStringLiteral("fixture-old-key"));
        QVERIFY(controller.modelConfigured());
        QVERIFY(controller.configurationDirty());
        QCOMPARE(notifications.size(), 1);
        QCOMPARE(notifications.at(0).at(1).toString(), QStringLiteral("error"));
        QVERIFY(notifications.at(0).at(0).toString().contains(QStringLiteral("访问权限")));
        QVERIFY(!notifications.at(0).at(0).toString().contains(QStringLiteral("fixture-new-key")));
        const QSettings originalSettings(QDir(preservedDirectory).filePath(QStringLiteral("config.ini")),
                                         QSettings::IniFormat);
        QCOMPARE(originalSettings.value(QStringLiteral("modelConnection/modelId")).toString(),
                 QStringLiteral("deepseek-flash"));
        QCOMPARE(originalSettings.value(QStringLiteral("modelConnection/apiKey")).toString(),
                 QStringLiteral("fixture-old-key"));
    }

    void rejectsMalformedCredentialsWithoutNetworking()
    {
        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key\r\nX-Fixture: value"));
        QSignalSpy notifications(&controller, &ModelConnectionController::notification);

        QVERIFY(!controller.saveModel());
        QVERIFY(!controller.testConnection());
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("unconfigured"));
        QVERIFY(QSettings(configFilePath(), QSettings::IniFormat).allKeys().isEmpty());
        QVERIFY(controller.savedApiKey().isEmpty());
        QCOMPARE(notifications.size(), 1);
        QCOMPARE(notifications.at(0).at(1).toString(), QStringLiteral("error"));
        QVERIFY(!notifications.at(0).at(0).toString().contains(QStringLiteral("fixture-key")));
    }

    void reportsAnInvalidStoredKeyWithoutNetworking()
    {
        QSettings settings(configFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("modelConnection/apiKey"), QStringLiteral("fixture-key with spaces"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        QVERIFY(!controller.testConnection());
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
        QCOMPARE(controller.verificationStatus(), QStringLiteral("failed"));
        QVERIFY(controller.verificationMessage().contains(QStringLiteral("API Key 格式无效")));
        QVERIFY(!controller.verificationMessage().contains(QStringLiteral("fixture-key")));
    }

    void failedResponsesUseSafeFeedback_data()
    {
        QTest::addColumn<int>("httpStatus");
        QTest::addColumn<int>("networkError");
        QTest::addColumn<QByteArray>("body");
        const QByteArray errorBody = R"({"error":{"message":"fixture-key secret provider details"}})";
        QTest::newRow("unauthorized") << 401 << int(QNetworkReply::AuthenticationRequiredError) << errorBody;
        QTest::newRow("forbidden") << 403 << int(QNetworkReply::ContentAccessDenied) << errorBody;
        QTest::newRow("rate-limited") << 429 << int(QNetworkReply::UnknownContentError) << errorBody;
        QTest::newRow("unavailable") << 503 << int(QNetworkReply::ServiceUnavailableError) << errorBody;
        QTest::newRow("network-failure") << 0 << int(QNetworkReply::ConnectionRefusedError) << errorBody;
        QTest::newRow("tls-failure") << 0 << int(QNetworkReply::SslHandshakeFailedError) << errorBody;
        QTest::newRow("timeout") << 0 << int(QNetworkReply::TimeoutError) << errorBody;
        QTest::newRow("redirect") << 302 << int(QNetworkReply::NoError) << ModelList;
        QTest::newRow("malformed") << 200 << int(QNetworkReply::NoError) << QByteArray("fixture-key invalid JSON");
        QTest::newRow("unexpected-shape") << 200 << int(QNetworkReply::NoError) << errorBody;
    }

    void failedResponsesUseSafeFeedback()
    {
        QFETCH(int, httpStatus);
        QFETCH(int, networkError);
        QFETCH(QByteArray, body);

        FakeNetwork network;
        ModelConnectionController controller(configFilePath(), &network);
        controller.setDraftApiKey(QStringLiteral("fixture-key"));
        QVERIFY(controller.saveModel());
        QVERIFY(network.reply);
        network.reply->complete(httpStatus, body, QNetworkReply::NetworkError(networkError));

        QCOMPARE(controller.verificationStatus(), QStringLiteral("failed"));
        const QString message = controller.verificationMessage();
        QVERIFY(!message.isEmpty());
        if (httpStatus == 401)
            QVERIFY(message.contains(QStringLiteral("API Key 无效")));
        QVERIFY(!message.contains(QStringLiteral("fixture-key")));
        QVERIFY(!message.contains(QStringLiteral("provider details")));
        QVERIFY(!message.contains(QStringLiteral("synthetic reply details")));

        FakeNetwork restoredNetwork;
        ModelConnectionController reopened(configFilePath(), &restoredNetwork);
        QCOMPARE(reopened.verificationStatus(), QStringLiteral("failed"));
        QCOMPARE(reopened.verificationMessage(), message);
        QVERIFY(reopened.verificationRestored());
        QCOMPARE(restoredNetwork.operation, QNetworkAccessManager::UnknownOperation);
        const QSettings settings(configFilePath(), QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("modelConnection/verification/status")).toString(),
                 QStringLiteral("failed"));
        const auto digest = settings.value(QStringLiteral("modelConnection/verification/profileDigest")).toString();
        QCOMPARE(digest.size(), 64);
        const auto code = settings.value(QStringLiteral("modelConnection/verification/errorCode")).toString();
        QVERIFY(!code.isEmpty());
        QVERIFY(!code.contains(QStringLiteral("fixture-key")));
        QVERIFY(!code.contains(QStringLiteral("provider details")));
        QVERIFY(!code.contains(QStringLiteral("synthetic reply details")));
    }

private:
    QString configFilePath() const
    {
        return m_settings->filePath(QStringLiteral("chunfeng/config.ini"));
    }

    std::unique_ptr<QTemporaryDir> m_settings;
};

QTEST_GUILESS_MAIN(ModelConnectionControllerTests)

#include "model_connection_controller_tests.moc"
