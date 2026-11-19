#include "fake_network.h"
#include "model_connection_controller.h"
#include "model_test_chat_controller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using TestNetwork::FakeNetwork;

const QByteArray Reply = R"(data: {"id":"chat-fixture","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"**回答**"},"finish_reason":null}]})" "\n\n";
const QByteArray Done = R"(data: {"id":"chat-fixture","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]})" "\n\n"
                        "data: [DONE]\n\n";

bool saveFixtureProfile(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("modelConnection/modelId"), QStringLiteral("deepseek-v4-pro"));
    settings.setValue(QStringLiteral("modelConnection/apiKey"), QStringLiteral("fixture-saved-key"));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QVariant turnValue(const QAbstractItemModel *turns, int row, const QByteArray &role)
{
    return turns->data(turns->index(row, 0), turns->roleNames().key(role, -1));
}

QJsonArray requestMessages(const QByteArray &body)
{
    return QJsonDocument::fromJson(body).object().value(QStringLiteral("messages")).toArray();
}
}

class ModelTestChatControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void configuredKeyCanChatWithoutChangingFailedVerification()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString config = workspace.filePath(QStringLiteral("config.ini"));
        FakeNetwork verificationNetwork;
        FakeNetwork chatNetwork;
        {
            ModelConnectionController unconfigured(config, &verificationNetwork);
            ModelTestChatController chat(unconfigured, &chatNetwork);
            QVERIFY(!chat.beginSession());
            QVERIFY(!chat.sendPrompt(QStringLiteral("No session")));
            QCOMPARE(chatNetwork.operation, QNetworkAccessManager::UnknownOperation);
        }
        QVERIFY(saveFixtureProfile(config));
        ModelConnectionController model(config, &verificationNetwork);
        QVERIFY(model.testConnection());
        QVERIFY(verificationNetwork.reply);
        verificationNetwork.reply->complete(401, {}, QNetworkReply::AuthenticationRequiredError);
        QCOMPARE(model.verificationStatus(), QStringLiteral("failed"));
        const QString verificationMessage = model.verificationMessage();
        QSignalSpy verificationChanges(&model, &ModelConnectionController::verificationStatusChanged);
        model.setDraftApiKey(QStringLiteral("fixture-unsaved-key"));
        model.setDraftModelName(QStringLiteral("deepseek-flash"));

        ModelTestChatController chat(model, &chatNetwork);
        QVERIFY(chat.beginSession());
        QVERIFY(chat.active());
        QCOMPARE(chat.modelName(), QStringLiteral("deepseek-v4-pro"));
        QVERIFY(!chat.sendPrompt(QStringLiteral(" \n\t ")));
        QCOMPARE(chatNetwork.operation, QNetworkAccessManager::UnknownOperation);
        QVERIFY(chat.sendPrompt(QStringLiteral(" First line\n第二行 ")));
        QVERIFY(chat.running());
        QCOMPARE(chatNetwork.operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(chatNetwork.request.url(), QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
        QCOMPARE(chatNetwork.request.rawHeader("Authorization"), QByteArray("Bearer fixture-saved-key"));
        const QJsonObject request = QJsonDocument::fromJson(chatNetwork.body).object();
        QCOMPARE(request.value("model"), QJsonValue(QStringLiteral("deepseek-v4-pro")));
        QCOMPARE(request.value("stream"), QJsonValue(true));
        QCOMPARE(requestMessages(chatNetwork.body), QJsonArray({QJsonObject{
            {"role", "user"}, {"content", " First line\n第二行 "}
        }}));
        QVERIFY(chatNetwork.reply);
        auto *reply = chatNetwork.reply;
        QVERIFY(!chat.sendPrompt(QStringLiteral("While busy")));
        QCOMPARE(chatNetwork.reply, reply);
        reply->deliver(200, Reply);
        QCOMPARE(turnValue(chat.turns(), 0, "replyText").toString(), QStringLiteral("**回答**"));
        QCOMPARE(turnValue(chat.turns(), 0, "turnState").toString(), QStringLiteral("streaming"));
        reply->complete(200, Done);
        QVERIFY(!chat.running());
        QCOMPARE(turnValue(chat.turns(), 0, "turnState").toString(), QStringLiteral("complete"));
        QCOMPARE(model.verificationStatus(), QStringLiteral("failed"));
        QCOMPARE(model.verificationMessage(), verificationMessage);
        QVERIFY(verificationChanges.isEmpty());
    }

    void chatSkipsFailedPairsAndRetriesTheLatestRowInPlace()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString config = workspace.filePath(QStringLiteral("config.ini"));
        QVERIFY(saveFixtureProfile(config));
        FakeNetwork verificationNetwork;
        FakeNetwork chatNetwork;
        ModelConnectionController model(config, &verificationNetwork);
        ModelTestChatController chat(model, &chatNetwork);
        QVERIFY(chat.beginSession());
        QVERIFY(!chat.retryLast());
        QVERIFY(chat.sendPrompt(QStringLiteral("Completed first")));
        QVERIFY(chatNetwork.reply);
        chatNetwork.reply->complete(200, Reply + Done);
        QVERIFY(!chat.retryLast());

        QVERIFY(chat.sendPrompt(QStringLiteral("Failed second")));
        QVERIFY(chatNetwork.reply);
        chatNetwork.reply->complete(200, Reply);
        QCOMPARE(turnValue(chat.turns(), 1, "turnState").toString(), QStringLiteral("failed"));
        QCOMPARE(turnValue(chat.turns(), 1, "replyText").toString(), QStringLiteral("**回答**"));
        QVERIFY(!turnValue(chat.turns(), 1, "turnMessage").toString().isEmpty());

        QVERIFY(chat.sendPrompt(QStringLiteral("Retry the third")));
        QVERIFY(chatNetwork.reply);
        const QJsonArray expected{
            QJsonObject{{"role", "user"}, {"content", "Completed first"}},
            QJsonObject{{"role", "assistant"}, {"content", "**回答**"}},
            QJsonObject{{"role", "user"}, {"content", "Retry the third"}}
        };
        QCOMPARE(requestMessages(chatNetwork.body), expected);
        chatNetwork.reply->complete(200, Reply);
        auto *turns = chat.turns();
        const QPersistentModelIndex thirdTurn(turns->index(2, 0));
        QVERIFY(chat.retryLast());
        QVERIFY(!chat.retryLast());
        QCOMPARE(turns->rowCount(), 3);
        QVERIFY(thirdTurn.isValid());
        QCOMPARE(turnValue(turns, 2, "promptText").toString(), QStringLiteral("Retry the third"));
        QCOMPARE(turnValue(turns, 2, "turnState").toString(), QStringLiteral("streaming"));
        QVERIFY(turnValue(turns, 2, "replyText").toString().isEmpty());
        QCOMPARE(requestMessages(chatNetwork.body), expected);
        QVERIFY(chatNetwork.reply);
        chatNetwork.reply->complete(200, Reply + Done);
        QCOMPARE(turnValue(turns, 2, "turnState").toString(), QStringLiteral("complete"));
        QCOMPARE(verificationNetwork.operation, QNetworkAccessManager::UnknownOperation);
        QCOMPARE(model.verificationStatus(), QStringLiteral("untested"));
    }

    void closingCancelsAndClearsWhileNextSessionUsesNewSavedProfile()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString config = workspace.filePath(QStringLiteral("config.ini"));
        QVERIFY(saveFixtureProfile(config));
        FakeNetwork verificationNetwork;
        FakeNetwork chatNetwork;
        ModelConnectionController model(config, &verificationNetwork);
        ModelTestChatController chat(model, &chatNetwork);
        QVERIFY(chat.beginSession());
        model.setDraftApiKey(QStringLiteral("fixture-next-key"));
        model.setDraftModelName(QStringLiteral("deepseek-flash"));
        QVERIFY(model.saveModel());
        QCOMPARE(model.verificationStatus(), QStringLiteral("testing"));

        QVERIFY(chat.sendPrompt(QStringLiteral("First session")));
        auto *oldReply = chatNetwork.reply;
        QVERIFY(oldReply);
        QCOMPARE(chatNetwork.request.rawHeader("Authorization"), QByteArray("Bearer fixture-saved-key"));
        QCOMPARE(chat.modelName(), QStringLiteral("deepseek-v4-pro"));
        oldReply->deliver(200, Reply);
        chat.endSession();
        QCOMPARE(oldReply->error(), QNetworkReply::OperationCanceledError);
        QCOMPARE(chat.turns()->rowCount(), 0);
        QVERIFY(!chat.active());
        QVERIFY(!chat.running());
        QVERIFY(chat.modelName().isEmpty());

        QVERIFY(chat.beginSession());
        QCOMPARE(chat.modelName(), QStringLiteral("deepseek-flash"));
        QVERIFY(chat.sendPrompt(QStringLiteral("Fresh session")));
        QCOMPARE(chatNetwork.request.rawHeader("Authorization"), QByteArray("Bearer fixture-next-key"));
        QCOMPARE(requestMessages(chatNetwork.body), QJsonArray({QJsonObject{
            {"role", "user"}, {"content", "Fresh session"}
        }}));
        oldReply->complete(200, Reply + Done);
        QVERIFY(chat.running());
        QCOMPARE(chat.turns()->rowCount(), 1);
        QVERIFY(turnValue(chat.turns(), 0, "replyText").toString().isEmpty());
        QCOMPARE(model.verificationStatus(), QStringLiteral("testing"));
    }
};

QTEST_GUILESS_MAIN(ModelTestChatControllerTests)

#include "model_test_chat_controller_tests.moc"
