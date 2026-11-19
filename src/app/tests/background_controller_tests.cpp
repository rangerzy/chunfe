#include "background_analysis_controller.h"
#include "background_controller.h"
#include "fake_network.h"
#include "model_connection_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
using TestNetwork::FakeNetwork;

constexpr QLatin1StringView PromptKey{"backgroundAnalysis/prompt"};
const QByteArray AnalysisChunk = R"(data: {"id":"analysis-fixture","object":"chat.completion.chunk","created":1,"model":"deepseek-v4-pro","choices":[{"index":0,"delta":{"content":"**要点**"},"finish_reason":null}]})" "\n\n";
const QByteArray EndOfAnalysis = R"(data: {"id":"analysis-fixture","object":"chat.completion.chunk","created":1,"model":"deepseek-v4-pro","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]})" "\n\n"
                                 "data: [DONE]\n\n";
const QByteArray ModelList = R"({"object":"list","data":[{"id":"deepseek-v4-pro","object":"model","owned_by":"deepseek"}]})";

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

class AnalysisFixture
{
public:
    bool importMarkdown(const QByteArray &markdown)
    {
        const QString source = workspace.filePath(QStringLiteral("source.md"));
        return writeFile(source, markdown) && document.importMarkdown(QUrl::fromLocalFile(source));
    }

    bool saveProfile()
    {
        model.setDraftModelName(QStringLiteral("deepseek-v4-pro"));
        model.setDraftApiKey(QStringLiteral("fixture-saved-key"));
        return model.saveModel();
    }

    bool updatePrompt(const QString &value)
    {
        QSettings settings(configFile, QSettings::IniFormat);
        settings.setValue(PromptKey, value);
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    QTemporaryDir workspace;
    const QString dataDirectory = workspace.filePath(QStringLiteral("chunfeng"));
    const QString configFile = QDir(dataDirectory).filePath(QStringLiteral("config.ini"));
    FakeNetwork analysisNetwork;
    FakeNetwork connectionNetwork;
    BackgroundController document{dataDirectory};
    ModelConnectionController model{configFile, &connectionNetwork};
    BackgroundAnalysisController analysis{configFile, document, model, &analysisNetwork};
};
}

class BackgroundControllerTests : public QObject
{
    Q_OBJECT

private slots:
    void preservesMarkdownBytesAndRestoresTheManagedCopy()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString dataDirectory = workspace.filePath(QStringLiteral("new/chunfeng"));
        const QString source = workspace.filePath(QStringLiteral("notes.MARKDOWN"));
        const QString literalText = QStringLiteral("# 项目\r\n\r\n**原文** <b>tag</b>\r\n");
        const QByteArray bytes = QByteArray::fromHex("efbbbf") + literalText.toUtf8();
        QVERIFY(writeFile(source, bytes));

        BackgroundController controller(dataDirectory);
        QVERIFY(controller.backgroundText().isEmpty());
        QVERIFY(controller.errorMessage().isEmpty());
        QVERIFY(!QFileInfo::exists(dataDirectory));
        QSignalSpy changed(&controller, &BackgroundController::backgroundTextChanged);
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(source)));
        QCOMPARE(controller.backgroundText(), literalText);
        QCOMPARE(changed.size(), 1);
        QVERIFY(controller.errorMessage().isEmpty());

        const QString managed = QDir(dataDirectory).filePath(QStringLiteral("background.md"));
        QCOMPARE(readFile(managed), bytes);
        QVERIFY(writeFile(source, "This changed outside the app."));
        BackgroundController reopened(dataDirectory);
        QCOMPARE(reopened.backgroundText(), literalText);
        QVERIFY(reopened.errorMessage().isEmpty());
    }

    void replacesManagedCopyAtTheSizeLimit()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString dataDirectory = workspace.filePath(QStringLiteral("chunfeng"));
        const QString first = workspace.filePath(QStringLiteral("first.md"));
        const QString second = workspace.filePath(QStringLiteral("second.MD"));
        QVERIFY(writeFile(first, "first text"));
        const QByteArray replacement(256 * 1024, 'x');
        QVERIFY(writeFile(second, replacement));

        BackgroundController controller(dataDirectory);
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(first)));
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(second)));
        QCOMPARE(controller.backgroundText(), QString::fromUtf8(replacement));
        QVERIFY(controller.errorMessage().isEmpty());
        QCOMPARE(readFile(QDir(dataDirectory).filePath(QStringLiteral("background.md"))), replacement);
    }

    void rejectsInvalidSourcesWithoutReplacingThePreviousCopy_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("unsupported-extension") << QStringLiteral("notes.txt") << QByteArray("text");
        QTest::newRow("invalid-utf8") << QStringLiteral("notes.md") << QByteArray::fromHex("23ff");
        QTest::newRow("truncated-utf8") << QStringLiteral("notes.md") << QByteArray::fromHex("23e4");
        QTest::newRow("binary-nul") << QStringLiteral("notes.md") << QByteArray("x\0x", 3);
        QTest::newRow("whitespace") << QStringLiteral("notes.md") << QByteArray("  \t\r\n");
        QTest::newRow("bom-only") << QStringLiteral("notes.md") << QByteArray::fromHex("efbbbf");
        QTest::newRow("oversized")
            << QStringLiteral("notes.md") << QByteArray(256 * 1024 + 1, 'x');
    }

    void rejectsInvalidSourcesWithoutReplacingThePreviousCopy()
    {
        QFETCH(QString, fileName);
        QFETCH(QByteArray, bytes);
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString original = workspace.filePath(QStringLiteral("original.md"));
        const QString rejected = workspace.filePath(fileName);
        QVERIFY(writeFile(original, "previous background"));
        QVERIFY(writeFile(rejected, bytes));

        const QString dataDirectory = workspace.filePath(QStringLiteral("chunfeng"));
        BackgroundController controller(dataDirectory);
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(original)));
        QSignalSpy changed(&controller, &BackgroundController::backgroundTextChanged);
        QVERIFY(!controller.importMarkdown(QUrl::fromLocalFile(rejected)));
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(changed.isEmpty());
        QCOMPARE(controller.backgroundText(), QStringLiteral("previous background"));
        QCOMPARE(readFile(QDir(dataDirectory).filePath(QStringLiteral("background.md"))),
                 QByteArray("previous background"));
    }

    void reportsReadErrorsAndLeavesCanceledImportsAlone()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString source = workspace.filePath(QStringLiteral("notes.md"));
        QVERIFY(writeFile(source, "example"));
        BackgroundController controller(workspace.filePath(QStringLiteral("chunfeng")));
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(source)));
        QVERIFY(!controller.importMarkdown(QUrl(QStringLiteral("https://example.invalid/example.md"))));
        QVERIFY(!controller.errorMessage().isEmpty());
        QCOMPARE(controller.backgroundText(), QStringLiteral("example"));

        const QUrl missingFile = QUrl::fromLocalFile(workspace.filePath(QStringLiteral("missing.md")));
        QVERIFY(!controller.importMarkdown(missingFile));
        const QString readError = controller.errorMessage();
        QVERIFY(!readError.isEmpty());
        QVERIFY(!controller.importMarkdown(QUrl()));
        QCOMPARE(controller.errorMessage(), readError);
        QCOMPARE(controller.backgroundText(), QStringLiteral("example"));
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(source)));
        QVERIFY(controller.errorMessage().isEmpty());
    }

    void reportsInvalidSavedFileAtStartup()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        QVERIFY(writeFile(workspace.filePath(QStringLiteral("background.md")), QByteArray::fromHex("ff")));
        BackgroundController controller(workspace.path());
        QVERIFY(controller.backgroundText().isEmpty());
        QVERIFY(!controller.errorMessage().isEmpty());
    }

    void failedReplacementPreservesThePriorBackground()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString dataDirectory = workspace.filePath(QStringLiteral("chunfeng"));
        const QString preservedDirectory = workspace.filePath(QStringLiteral("preserved"));
        const QString source = workspace.filePath(QStringLiteral("notes.md"));
        QVERIFY(writeFile(source, "previous background"));
        BackgroundController controller(dataDirectory);
        QVERIFY(controller.importMarkdown(QUrl::fromLocalFile(source)));

        QVERIFY(QDir().rename(dataDirectory, preservedDirectory));
        QVERIFY(writeFile(dataDirectory, "blocks directory creation"));
        QVERIFY(writeFile(source, "new background"));
        QSignalSpy changed(&controller, &BackgroundController::backgroundTextChanged);
        QVERIFY(!controller.importMarkdown(QUrl::fromLocalFile(source)));
        QVERIFY(!controller.errorMessage().isEmpty());
        QVERIFY(changed.isEmpty());
        QCOMPARE(controller.backgroundText(), QStringLiteral("previous background"));
        QCOMPARE(readFile(QDir(preservedDirectory).filePath(QStringLiteral("background.md"))),
                 QByteArray("previous background"));
    }

    void analysisSeedsPromptAndSendsTheCurrentTemplateUsingTheSavedProfile()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QSettings settings(fixture.configFile, QSettings::IniFormat);
        QVERIFY(settings.value(PromptKey).toString().contains(QStringLiteral("{{background}}")));
        QVERIFY(fixture.importMarkdown("# 背景\r\n\r\n- **原文**\r\n"));
        QVERIFY(fixture.saveProfile());
        fixture.model.setDraftModelName(QStringLiteral("deepseek-flash"));
        fixture.model.setDraftApiKey(QStringLiteral("fixture-unsaved-key"));
        QVERIFY(fixture.updatePrompt(QStringLiteral("Read this Markdown:\n{{background}}\nEnd.")));

        QVERIFY(fixture.analysis.configured());
        QVERIFY(fixture.analysis.startAnalysis());
        QVERIFY(fixture.analysis.running());
        QCOMPARE(fixture.analysisNetwork.operation, QNetworkAccessManager::PostOperation);
        QCOMPARE(fixture.analysisNetwork.request.url(), QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
        QCOMPARE(fixture.analysisNetwork.request.rawHeader("Authorization"), QByteArray("Bearer fixture-saved-key"));
        const auto payload = QJsonDocument::fromJson(fixture.analysisNetwork.body).object();
        QCOMPARE(payload.value("model").toString(), QStringLiteral("deepseek-v4-pro"));
        QCOMPARE(payload.value("stream"), QJsonValue(true));
        QCOMPARE(payload.value("max_tokens").toInt(), 2048);
        QCOMPARE(payload.value("messages").toArray(), QJsonArray({QJsonObject{
            {"role", "user"}, {"content", "Read this Markdown:\n# 背景\r\n\r\n- **原文**\r\n\nEnd."}
        }}));
        QCOMPARE(fixture.model.verificationStatus(), QStringLiteral("testing"));

        const QByteArray frame = ": keep-alive\r\n\r\n"
            R"(data: {"choices":[{"delta":{"role":"assistant","reasoning_content":"not reply text"}}]})" "\r\n\r\n"
            + QByteArray(AnalysisChunk).replace("\n", "\r\n");
        const qsizetype split = frame.indexOf("要") + 1;
        QVERIFY(split > 0);
        QVERIFY(fixture.analysisNetwork.reply);
        fixture.analysisNetwork.reply->deliver(200, frame.first(split));
        QVERIFY(fixture.analysis.resultText().isEmpty());
        fixture.analysisNetwork.reply->deliver(200, frame.sliced(split).chopped(1));
        QVERIFY(fixture.analysis.resultText().isEmpty());
        fixture.analysisNetwork.reply->deliver(200, "\n");
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));
        QVERIFY(fixture.analysis.running());
        fixture.analysisNetwork.reply->complete(200, EndOfAnalysis);
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));
        QVERIFY(!fixture.analysis.running());
        QVERIFY(!fixture.analysis.failed());

        QVERIFY(fixture.updatePrompt(QStringLiteral("New instruction: {{background}}")));
        QVERIFY(fixture.analysis.startAnalysis());
        QVERIFY(fixture.analysis.resultText().isEmpty());
        const auto next = QJsonDocument::fromJson(fixture.analysisNetwork.body).object();
        QCOMPARE(next.value("messages").toArray().first().toObject().value("content").toString(),
                 QStringLiteral("New instruction: # 背景\r\n\r\n- **原文**\r\n"));
    }

    void analysisRejectsAnUnsavedKeyWithoutSending()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# Visible background"));
        fixture.model.setDraftApiKey(QStringLiteral("fixture-unsaved-key"));

        QVERIFY(!fixture.analysis.configured());
        QVERIFY(!fixture.analysis.startAnalysis());
        QCOMPARE(fixture.analysisNetwork.operation, QNetworkAccessManager::UnknownOperation);

        QVERIFY(fixture.saveProfile());
        QVERIFY(fixture.analysis.configured());
    }

    void analysisRejectsMissingPromptPlaceholderWithoutSending()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# Visible background"));
        QVERIFY(fixture.saveProfile());
        QVERIFY(fixture.updatePrompt(QStringLiteral("This prompt omits the replacement field.")));

        QVERIFY(!fixture.analysis.startAnalysis());
        QVERIFY(fixture.analysis.failed());
        QVERIFY(!fixture.analysis.running());
        QVERIFY(fixture.analysis.statusMessage().contains(QStringLiteral("{{background}}")));
        QCOMPARE(fixture.analysisNetwork.operation, QNetworkAccessManager::UnknownOperation);

        QSettings settings(fixture.configFile, QSettings::IniFormat);
        QCOMPARE(settings.value(PromptKey).toString(), QStringLiteral("This prompt omits the replacement field."));
    }

    void analysisCancellationKeepsPartialTextAndDoesNotAffectConnectionTest()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# Visible background"));
        QVERIFY(fixture.saveProfile());
        QCOMPARE(fixture.model.verificationStatus(), QStringLiteral("testing"));
        QVERIFY(fixture.analysis.startAnalysis());
        auto *analysisReply = fixture.analysisNetwork.reply;
        QVERIFY(analysisReply);
        analysisReply->deliver(200, AnalysisChunk);

        fixture.analysis.cancelAnalysis();
        QCOMPARE(analysisReply->error(), QNetworkReply::OperationCanceledError);
        QVERIFY(!fixture.analysis.running());
        QVERIFY(!fixture.analysis.failed());
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));
        analysisReply->deliver(200, AnalysisChunk + EndOfAnalysis);
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));

        QCOMPARE(fixture.model.verificationStatus(), QStringLiteral("testing"));
        QVERIFY(fixture.connectionNetwork.reply);
        fixture.connectionNetwork.reply->complete(200, ModelList);
        QCOMPARE(fixture.model.verificationStatus(), QStringLiteral("passed"));
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));
        QVERIFY(!fixture.analysis.running());
    }

    void importingADifferentDocumentAbortsAndClearsAnalysis()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# First document"));
        QVERIFY(fixture.saveProfile());
        QVERIFY(fixture.analysis.startAnalysis());
        auto *analysisReply = fixture.analysisNetwork.reply;
        QVERIFY(analysisReply);
        analysisReply->deliver(200, AnalysisChunk);
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));

        QVERIFY(fixture.importMarkdown("# Second document"));
        QCOMPARE(analysisReply->error(), QNetworkReply::OperationCanceledError);
        QVERIFY(!fixture.analysis.running());
        QVERIFY(!fixture.analysis.failed());
        QVERIFY(fixture.analysis.resultText().isEmpty());
        analysisReply->deliver(200, AnalysisChunk + EndOfAnalysis);
        QVERIFY(fixture.analysis.resultText().isEmpty());
    }

    void unauthorizedAnalysisReportsSafeFeedback()
    {
        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# Visible background"));
        QVERIFY(fixture.saveProfile());
        QVERIFY(fixture.analysis.startAnalysis());
        QVERIFY(fixture.analysisNetwork.reply);
        fixture.analysisNetwork.reply->complete(401, R"({"error":{"message":"fixture-saved-key provider details"}})",
                                                QNetworkReply::AuthenticationRequiredError);
        QVERIFY(!fixture.analysis.running());
        QVERIFY(fixture.analysis.failed());
        QVERIFY(fixture.analysis.statusMessage().contains(QStringLiteral("API Key")));
        QVERIFY(!fixture.analysis.statusMessage().contains(QStringLiteral("fixture-saved-key")));
        QVERIFY(!fixture.analysis.statusMessage().contains(QStringLiteral("provider details")));
    }

    void terminalStreamOutcomesKeepPartialAnalysis_data()
    {
        QTest::addColumn<QByteArray>("tail");
        QTest::addColumn<int>("networkError");
        QTest::addColumn<bool>("failed");
        QTest::addColumn<QString>("messageFragment");
        const int noError = int(QNetworkReply::NoError);
        const auto finishFrame = [](const QByteArray &reason) {
            return "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"" + reason + "\"}]}\n\ndata: [DONE]\n\n";
        };

        QTest::newRow("missing-done") << QByteArray() << noError << true << QStringLiteral("提前结束");
        QTest::newRow("stream-error")
            << QByteArray(R"(data: {"error":{"message":"fixture-saved-key provider details"}})" "\n\n")
            << noError << true << QStringLiteral("中断");
        QTest::newRow("malformed")
            << QByteArray("data: {fixture-saved-key\n\n") << noError << true << QStringLiteral("有效");
        QTest::newRow("network-error")
            << QByteArray() << int(QNetworkReply::RemoteHostClosedError) << true << QStringLiteral("检查网络");
        QTest::newRow("provider-aborted")
            << finishFrame("aborted") << noError << true << QStringLiteral("中断");
        QTest::newRow("unexpected-tool-calls")
            << finishFrame("tool_calls") << noError << true << QStringLiteral("文本");
        QTest::newRow("length-limit")
            << finishFrame("length") << noError << false << QStringLiteral("达到长度上限");
    }

    void terminalStreamOutcomesKeepPartialAnalysis()
    {
        QFETCH(QByteArray, tail);
        QFETCH(int, networkError);
        QFETCH(bool, failed);
        QFETCH(QString, messageFragment);

        AnalysisFixture fixture;
        QVERIFY(fixture.workspace.isValid());
        QVERIFY(fixture.importMarkdown("# Visible background"));
        QVERIFY(fixture.saveProfile());
        QVERIFY(fixture.analysis.startAnalysis());
        QVERIFY(fixture.analysisNetwork.reply);
        fixture.analysisNetwork.reply->deliver(200, AnalysisChunk);
        fixture.analysisNetwork.reply->complete(200, tail, QNetworkReply::NetworkError(networkError));
        QVERIFY(!fixture.analysis.running());
        QCOMPARE(fixture.analysis.failed(), failed);
        QCOMPARE(fixture.analysis.resultText(), QStringLiteral("**要点**"));
        const QString message = fixture.analysis.statusMessage();
        QVERIFY(message.contains(messageFragment));
        QVERIFY(!message.contains(QStringLiteral("fixture-saved-key")));
        QVERIFY(!message.contains(QStringLiteral("provider details")));
        QVERIFY(!message.contains(QStringLiteral("synthetic reply details")));
    }
};

QTEST_GUILESS_MAIN(BackgroundControllerTests)

#include "background_controller_tests.moc"
