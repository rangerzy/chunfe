#include "conversation_advice_controller.h"
#include "fake_network.h"
#include "model_connection_controller.h"

#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
using TestNetwork::FakeNetwork;

QVariantMap line(int id, const QString &source, const QString &text, bool final = true, bool incomplete = false)
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("source"), source}, {QStringLiteral("text"), text},
            {QStringLiteral("time"), QStringLiteral("00:07")}, {QStringLiteral("final"), final},
            {QStringLiteral("incomplete"), incomplete}};
}

void saveFixtureProfile(const QString &configFile)
{
    QSettings settings(configFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("modelConnection/modelId"), QStringLiteral("deepseek-flash"));
    settings.setValue(QStringLiteral("modelConnection/apiKey"), QStringLiteral("fixture-key"));
    settings.sync();
}

QString configuredPath(const QTemporaryDir &workspace)
{
    const QString path = workspace.filePath(QStringLiteral("config.ini"));
    if (workspace.isValid())
        saveFixtureProfile(path);
    return path;
}

struct Fixture {
    QTemporaryDir workspace;
    const QString configFile = configuredPath(workspace);
    FakeNetwork configurationNetwork;
    FakeNetwork network;
    ModelConnectionController model{configFile, &configurationNetwork};
    ConversationAdviceController controller{configFile, model, &network};
};

const QByteArray Done = "data: [DONE]\n\n";

QByteArray delta(const QString &text)
{
    const QJsonObject frame{
        {QStringLiteral("choices"),
         QJsonArray{QJsonObject{{QStringLiteral("delta"), QJsonObject{{QStringLiteral("content"), text}}}}}},
    };
    return "data: " + QJsonDocument(frame).toJson(QJsonDocument::Compact) + "\n\n";
}

bool editPrompt(const QString &path, const QString &prompt)
{
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("conversationAdvice/prompt"), prompt);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QJsonArray sentMessages(const FakeNetwork &network)
{
    return QJsonDocument::fromJson(network.body).object().value(QStringLiteral("messages")).toArray();
}

QString sentPrompt(const FakeNetwork &network)
{
    return sentMessages(network).at(1).toObject().value(QStringLiteral("content")).toString();
}
}

class ConversationAdviceControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void onlyFinalCompleteRemoteCandidatesStartRequests()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        auto &controller = fixture.controller;
        auto &network = fixture.network;
        controller.beginSession(QStringLiteral("本地背景"));
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);

        QVariantList lines{
            line(1, QStringLiteral("mic"), QStringLiteral("请解释一下你的项目职责。")),
            line(2, QStringLiteral("spk"), QStringLiteral("请介绍你最近的项目。"), false),
            line(3, QStringLiteral("spk"), QStringLiteral("请解释这项结果。"), true, true),
            line(4, QStringLiteral("spk"), QStringLiteral("你好。")),
        };
        controller.considerTranscript(lines);
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
        lines[1] = line(2, QStringLiteral("spk"), QStringLiteral("请介绍你最近的项目。"));
        controller.considerTranscript(lines);
        QCOMPARE(network.operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(network.request.url(), QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
        QVERIFY(!controller.generating());
    }

    void requestUsesFrozenProfileAndOnlyPrecedingConfirmedContext()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        auto &controller = fixture.controller;
        controller.beginSession(QStringLiteral("项目背景快照"));
        fixture.model.setDraftApiKey(QStringLiteral("fixture-later-key"));
        fixture.model.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        QVERIFY(fixture.model.saveModel());
        QCOMPARE(fixture.network.operation, QNetworkAccessManager::UnknownOperation);

        controller.considerTranscript({
            line(1, QStringLiteral("mic"), QStringLiteral("我负责了后端迁移。")),
            line(2, QStringLiteral("spk"), QStringLiteral("草稿不应发送。"), false),
            line(3, QStringLiteral("mic"), QStringLiteral("未完成不应发送。"), true, true),
            line(4, QStringLiteral("spk"), QStringLiteral("你怎样验证迁移的结果？")),
            line(5, QStringLiteral("mic"), QStringLiteral("较晚的转写不应发送。")),
        });
        QCOMPARE(fixture.network.request.rawHeader("Authorization"), QByteArray("Bearer fixture-key"));
        const auto request = QJsonDocument::fromJson(fixture.network.body).object();
        QCOMPARE(request.value(QStringLiteral("model")).toString(), QStringLiteral("deepseek-flash"));
        const auto messages = sentMessages(fixture.network);
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        const auto system = messages.at(0).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(system.contains(QStringLiteral("NONE")));
        QVERIFY(!system.contains(QStringLiteral("项目背景快照")));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        const auto prompt = sentPrompt(fixture.network);
        QVERIFY(prompt.contains(QStringLiteral("项目背景快照")));
        const auto contextHeading = QStringLiteral("\n当前对话上下文：\n");
        const auto headingPosition = prompt.indexOf(contextHeading);
        QVERIFY(headingPosition >= 0);
        const auto payload = QJsonDocument::fromJson(prompt.sliced(headingPosition + contextHeading.size()).toUtf8()).object();
        QCOMPARE(payload.value("candidate").toObject().value("source").toString(), QStringLiteral("spk"));
        QCOMPARE(payload.value("candidate").toObject().value("text").toString(), QStringLiteral("你怎样验证迁移的结果？"));
        const auto context = payload.value("context").toArray();
        QCOMPARE(context.size(), 1);
        QCOMPARE(context.at(0).toObject().value("source").toString(), QStringLiteral("mic"));
        QCOMPARE(context.at(0).toObject().value("text").toString(), QStringLiteral("我负责了后端迁移。"));
    }

    void displaysOnlyAdviceAndKeepsItAcrossIgnoredCandidates()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        auto &controller = fixture.controller;
        auto &network = fixture.network;
        controller.beginSession({});
        QVariantList lines{line(1, QStringLiteral("spk"), QStringLiteral("你如何验证结果？"))};
        controller.considerTranscript(lines);
        auto *first = network.reply;
        QVERIFY(first);
        controller.considerTranscript(lines);
        QCOMPARE(network.reply, first);
        QSignalSpy changes(&controller, &ConversationAdviceController::uiChanged);
        first->deliver(200, delta(QStringLiteral("QUESTION|如何验证结果？\n")));
        QVERIFY(!controller.generating());
        QVERIFY(changes.isEmpty());

        first->deliver(200, delta(QStringLiteral("先说明验证指标。")));
        QVERIFY(controller.generating());
        QCOMPARE(controller.triggerKind(), QStringLiteral("question"));
        QCOMPARE(controller.question(), QStringLiteral("如何验证结果？"));
        QCOMPARE(controller.answer(), QStringLiteral("先说明验证指标。"));
        first->complete(200, Done);
        QVERIFY(!controller.generating());
        changes.clear();

        lines.append(line(2, QStringLiteral("spk"), QStringLiteral("我们有两个可以比较的方案。")));
        controller.considerTranscript(lines);
        QVERIFY(network.reply && network.reply != first);
        network.reply->complete(200, delta(QStringLiteral("NONE")) + Done);
        QVERIFY(changes.isEmpty());
        QCOMPARE(controller.answer(), QStringLiteral("先说明验证指标。"));
        QCOMPARE(controller.finishSession().size(), 1);
    }

    void keepsTheConfiguredPromptUntilTheNextSession()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        const auto first = QStringLiteral("第一版口吻\n{{background}}\n{{context}}");
        const auto second = QStringLiteral("第二版口吻\n{{context}}\n{{background}}");
        QVERIFY(editPrompt(fixture.configFile, first));
        fixture.controller.beginSession(QStringLiteral("第一场背景"));
        QVERIFY(editPrompt(fixture.configFile, second));
        QCOMPARE(fixture.network.operation, QNetworkAccessManager::UnknownOperation);

        const auto candidate = line(1, QStringLiteral("spk"), QStringLiteral("请讲讲最近的项目。"));
        fixture.controller.considerTranscript({candidate});
        QVERIFY(sentPrompt(fixture.network).startsWith(QStringLiteral("第一版口吻\n")));
        QVERIFY(sentPrompt(fixture.network).contains(QStringLiteral("第一场背景")));
        QVERIFY(sentPrompt(fixture.network).contains(QStringLiteral("请讲讲最近的项目。")));
        QVERIFY(!sentPrompt(fixture.network).contains(QStringLiteral("{{context}}")));
        fixture.controller.finishSession();

        fixture.controller.beginSession(QStringLiteral("第二场背景"));
        fixture.controller.considerTranscript({candidate});
        QVERIFY(sentPrompt(fixture.network).startsWith(QStringLiteral("第二版口吻\n")));
        QVERIFY(sentPrompt(fixture.network).contains(QStringLiteral("第二场背景")));
        QVERIFY(!sentPrompt(fixture.network).contains(QStringLiteral("第一场背景")));
    }

    void reportsAnInvalidTemplateWithoutSendingDialogue()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(editPrompt(fixture.configFile, QStringLiteral("请结合 {{background}}。")));
        fixture.controller.beginSession(QStringLiteral("本地背景"));
        fixture.controller.considerTranscript({line(1, QStringLiteral("spk"), QStringLiteral("请介绍你最近的项目。"))});
        QCOMPARE(fixture.network.operation, QNetworkAccessManager::UnknownOperation);
        QVERIFY(fixture.controller.statusMessage().contains(QStringLiteral("{{context}}")));
        QVERIFY(fixture.controller.finishSession().isEmpty());
    }

    void pauseClearsQueuedCandidatesWithoutStoppingTheCurrentResponse()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        auto &controller = fixture.controller;
        auto &network = fixture.network;
        controller.beginSession({});
        QVariantList lines{line(1, QStringLiteral("spk"), QStringLiteral("请介绍第一步计划。")),
                           line(2, QStringLiteral("spk"), QStringLiteral("请介绍等待中的第二步计划。"))};
        controller.considerTranscript(lines);
        auto *active = network.reply;
        QVERIFY(active);
        controller.setListening(false);
        lines.append(line(3, QStringLiteral("spk"), QStringLiteral("请处理暂停期间的第三步。")));
        controller.considerTranscript(lines);
        QCOMPARE(active->error(), QNetworkReply::NoError);
        active->complete(200, delta(QStringLiteral("REQUEST|介绍计划\n先说明第一步。")) + Done);
        QCOMPARE(active->error(), QNetworkReply::NoError);
        QCOMPARE(network.reply, active);
        QCOMPARE(controller.answer(), QStringLiteral("先说明第一步。"));

        controller.setListening(true);
        controller.considerTranscript(lines);
        QCOMPARE(network.reply, active);
        lines.append(line(4, QStringLiteral("spk"), QStringLiteral("恢复后怎样快速开展下一步？")));
        controller.considerTranscript(lines);
        QVERIFY(network.reply && network.reply != active);
    }

    void finishKeepsCompletedAndVisiblePartialAdviceAndIgnoresLateReplies()
    {
        Fixture fixture;
        QVERIFY(fixture.workspace.isValid());
        auto &controller = fixture.controller;
        auto &network = fixture.network;
        controller.beginSession({});
        controller.considerTranscript({line(1, QStringLiteral("spk"), QStringLiteral("这个计划在一周内做不完。")),
                                       line(2, QStringLiteral("spk"), QStringLiteral("请说说有哪些替代方案。")),
                                       line(3, QStringLiteral("spk"), QStringLiteral("进度延误的原因是什么？"))});
        auto *first = network.reply;
        QVERIFY(first);
        first->complete(200, delta(QStringLiteral("OBJECTION|时间不够\n可以先划定最小范围。")) + Done);
        QVERIFY(network.reply && network.reply != first);
        auto *partial = network.reply;
        partial->deliver(200, delta(QStringLiteral("REQUEST|替代方案\n先比较两种做法。")));
        QVERIFY(controller.generating());
        const auto saved = controller.finishSession();
        QCOMPARE(saved.size(), 2);
        QCOMPARE(saved.at(0).toMap().value("kind").toString(), QStringLiteral("objection"));
        QVERIFY(!saved.at(0).toMap().value("incomplete").toBool());
        QCOMPARE(saved.at(1).toMap().value("answer").toString(), QStringLiteral("先比较两种做法。"));
        QVERIFY(saved.at(1).toMap().value("incomplete").toBool());
        QCOMPARE(partial->error(), QNetworkReply::OperationCanceledError);
        QVERIFY(!controller.generating());

        partial->complete(200, delta(QStringLiteral("这段迟到的文本不应出现。")) + Done);
        QCOMPARE(controller.answer(), QStringLiteral("先比较两种做法。"));
        QCOMPARE(controller.finishSession(), saved);
        QCOMPARE(network.reply, partial);
    }

    void missingKeyLeavesTheOfflineSessionUsable()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        FakeNetwork configurationNetwork;
        FakeNetwork network;
        const auto configFile = workspace.filePath(QStringLiteral("config.ini"));
        ModelConnectionController model(configFile, &configurationNetwork);
        ConversationAdviceController controller(configFile, model, &network);
        controller.beginSession(QStringLiteral("本地背景"));
        controller.considerTranscript({line(1, QStringLiteral("spk"), QStringLiteral("请介绍你最近的项目。"))});
        QCOMPARE(network.operation, QNetworkAccessManager::UnknownOperation);
        QVERIFY(!controller.statusMessage().isEmpty());
        QVERIFY(controller.finishSession().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ConversationAdviceControllerTests)

#include "conversation_advice_controller_tests.moc"
