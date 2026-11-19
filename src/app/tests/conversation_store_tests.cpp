#include "conversation_store.h"

#include <QFile>
#include <QJSEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {
QVariantMap sampleRecord(const QString &id, qint64 startedAt = 1795000000000)
{
    const QVariantList questions{
        QVariantMap{{"kind", QStringLiteral("question")},
                    {"question", QStringLiteral(" 对方的原始问题？\n")},
                    {"answer", QStringLiteral("**建议**\n第二行  ")},
                    {"incomplete", true}}};
    const QVariantList transcript{
        QVariantMap{{"source", QStringLiteral("left")}, {"role", QStringLiteral("other")},
                    {"text", QStringLiteral("  对方发言\n第二句。")}, {"time", QStringLiteral("00:01")},
                    {"final", true}, {"incomplete", false}},
        QVariantMap{{"source", QStringLiteral("right")}, {"role", QStringLiteral("me")},
                    {"text", QStringLiteral(" 我的发言还没说完 ")}, {"time", QStringLiteral("00:03")},
                    {"final", true}, {"incomplete", true}}};
    return {{"id", id}, {"title", QStringLiteral("讨论")},
            {"date", QStringLiteral("2026-11-18 20:00")}, {"duration", QStringLiteral("12 秒")},
            {"model", QStringLiteral("deepseek-flash")}, {"startedAt", startedAt},
            {"isDemo", false}, {"questions", questions}, {"transcript", transcript},
            {"backgroundText", QStringLiteral("# 背景快照\r\n\r\n  实例\n")}};
}
}

class ConversationStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void reopensWithLosslessDetailAndLightweightSummary()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString dataDirectory = workspace.filePath(QStringLiteral("new/chunfeng"));
        QVariantMap original;
        {
            ConversationStore store(dataDirectory);
            QVERIFY(store.ready());
            QVERIFY(store.records().isEmpty());
            original = sampleRecord(store.newRecordId());
            QVERIFY(!QUuid(original.value(QStringLiteral("id")).toString()).isNull());
            QVERIFY(store.saveRecord(original));
        }

        ConversationStore reopened(dataDirectory);
        QVERIFY(reopened.ready());
        QCOMPARE(reopened.records().size(), 1);
        const auto summary = reopened.records().first().toMap();
        QCOMPARE(summary.value(QStringLiteral("id")), original.value(QStringLiteral("id")));
        QCOMPARE(summary.value(QStringLiteral("questionCount")).toInt(), 1);
        QCOMPARE(summary.value(QStringLiteral("isDemo")).toBool(), false);
        QVERIFY(!summary.contains(QStringLiteral("transcript")));
        QVERIFY(!summary.contains(QStringLiteral("questions")));
        QVERIFY(!summary.contains(QStringLiteral("backgroundText")));
        QCOMPARE(reopened.record(summary.value(QStringLiteral("id")).toString()), original);
    }

    void databaseOrdersSharedRecordsByStartTime()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        ConversationStore first(workspace.path());
        ConversationStore second(workspace.path());
        QVERIFY(first.ready());
        QVERIFY(second.ready());

        QVERIFY(first.saveRecord(sampleRecord(QStringLiteral("middle"), 2000)));
        QVERIFY(second.saveRecord(sampleRecord(QStringLiteral("newest"), 3000)));
        QVERIFY(first.saveRecord(sampleRecord(QStringLiteral("oldest"), 1000)));

        const auto summaries = first.records();
        QCOMPARE(summaries.size(), 3);
        QCOMPARE(summaries.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("newest"));
        QCOMPARE(summaries.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("middle"));
        QCOMPARE(summaries.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("oldest"));
        QCOMPARE(second.record(QStringLiteral("oldest")).value(QStringLiteral("id")).toString(), QStringLiteral("oldest"));

        ConversationStore reopened(workspace.path());
        QCOMPARE(reopened.records(), summaries);
    }

    void acceptsNestedArraysFromJavaScript()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        ConversationStore store(workspace.path());
        QVERIFY(store.ready());
        QJSEngine engine;
        QJSEngine::setObjectOwnership(&store, QJSEngine::CppOwnership);
        engine.globalObject().setProperty(QStringLiteral("store"), engine.newQObject(&store));
        const auto saved = engine.evaluate(QStringLiteral(R"(
            store.saveRecord({
                id: "js-conversation", title: "Local JS fixture", date: "2026-11-18 20:00",
                duration: "12 秒", model: "", isDemo: false, startedAt: 1795000001234,
                questions: [{kind: "request", question: "  原始问题\n", answer: "建议", incomplete: true}],
                transcript: [{source: "right", role: "me", text: "  保留原文\n", time: "00:04",
                              final: true, incomplete: true}],
                backgroundText: ""
            })
        )"));
        QVERIFY2(!saved.isError(), qPrintable(saved.toString()));
        QVERIFY2(saved.toBool(), qPrintable(store.errorMessage()));

        const auto details = store.record(QStringLiteral("js-conversation"));
        QCOMPARE(details.value(QStringLiteral("startedAt")).toLongLong(), 1795000001234);
        const auto question = details.value(QStringLiteral("questions")).toList().value(0).toMap();
        const auto snippet = details.value(QStringLiteral("transcript")).toList().value(0).toMap();
        QCOMPARE(question.value(QStringLiteral("question")).toString(), QStringLiteral("  原始问题\n"));
        QCOMPARE(question.value(QStringLiteral("answer")).toString(), QStringLiteral("建议"));
        QVERIFY(question.value(QStringLiteral("incomplete")).toBool());
        QCOMPARE(snippet.value(QStringLiteral("text")).toString(), QStringLiteral("  保留原文\n"));
        QCOMPARE(snippet.value(QStringLiteral("source")).toString(), QStringLiteral("right"));
        QVERIFY(snippet.value(QStringLiteral("incomplete")).toBool());
    }

    void renameAndDeletePersistAndRefreshTheSummaries()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        auto original = sampleRecord(QStringLiteral("conversation"));
        const QString title = QStringLiteral("新的讨论 '周三'");
        {
            ConversationStore store(workspace.path());
            QVERIFY(store.ready());
            QSignalSpy changed(&store, &ConversationStore::recordsChanged);
            QVERIFY(store.saveRecord(original));
            QCOMPARE(changed.size(), 1);
            QVERIFY(store.renameRecord(QStringLiteral("conversation"), QStringLiteral("  ") + title + QStringLiteral("  ")));
            QCOMPARE(changed.size(), 2);
            QCOMPARE(store.records().first().toMap().value(QStringLiteral("title")).toString(), title);
        }

        {
            ConversationStore reopened(workspace.path());
            original.insert(QStringLiteral("title"), title);
            QCOMPARE(reopened.record(QStringLiteral("conversation")), original);
            QSignalSpy changed(&reopened, &ConversationStore::recordsChanged);
            QVERIFY(reopened.deleteRecord(QStringLiteral("conversation")));
            QCOMPARE(changed.size(), 1);
            QVERIFY(reopened.records().isEmpty());
            QVERIFY(reopened.record(QStringLiteral("conversation")).isEmpty());
        }

        ConversationStore reopened(workspace.path());
        QVERIFY(reopened.ready());
        QVERIFY(reopened.records().isEmpty());
    }

    void failedMutationsDoNotAlterStoredRecords()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        ConversationStore store(workspace.path());
        QVERIFY(store.ready());
        const auto original = sampleRecord(QStringLiteral("conversation"));
        QVERIFY(store.saveRecord(original));
        QSignalSpy changed(&store, &ConversationStore::recordsChanged);
        const auto summaries = store.records();

        auto duplicate = original;
        duplicate.insert(QStringLiteral("title"), QStringLiteral("不得覆盖原记录"));
        QVERIFY(!store.saveRecord(duplicate));
        QVERIFY(!store.errorMessage().isEmpty());
        QVERIFY(!store.errorMessage().contains(QStringLiteral("不得覆盖原记录")));

        auto demo = sampleRecord(QStringLiteral("demo"));
        demo.insert(QStringLiteral("isDemo"), true);
        QVERIFY(!store.saveRecord(demo));
        QVERIFY(!store.renameRecord(QStringLiteral("conversation"), QStringLiteral(" \n\t")));
        QVERIFY(!store.renameRecord(QStringLiteral("absent"), QStringLiteral("有效名称")));
        QVERIFY(!store.deleteRecord(QStringLiteral("absent")));
        QVERIFY(changed.isEmpty());
        QCOMPARE(store.records(), summaries);

        ConversationStore reopened(workspace.path());
        QCOMPARE(reopened.records(), summaries);
        QCOMPARE(reopened.record(QStringLiteral("conversation")), original);
        QVERIFY(reopened.record(QStringLiteral("demo")).isEmpty());
    }

    void unavailableDatabaseReportsAnErrorWithoutInventingHistory()
    {
        QTemporaryDir workspace;
        QVERIFY(workspace.isValid());
        const QString blockedDirectory = workspace.filePath(QStringLiteral("not-a-directory"));
        {
            QFile blocker(blockedDirectory);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
        }

        ConversationStore store(blockedDirectory);
        QVERIFY(!store.ready());
        QVERIFY(!store.errorMessage().isEmpty());
        QVERIFY(!store.errorMessage().contains(blockedDirectory));
        QVERIFY(store.records().isEmpty());
        QVERIFY(!store.saveRecord(sampleRecord(QStringLiteral("conversation"))));
        QVERIFY(store.record(QStringLiteral("conversation")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ConversationStoreTests)

#include "conversation_store_tests.moc"
