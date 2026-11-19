#include "conversation_advice_text.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using ConversationAdviceText::Response;

class ConversationAdviceTextTests final : public QObject {
    Q_OBJECT

private slots:
    void filtersOnlyStandaloneNoise()
    {
        for (const auto *text : {u"  ", u"……", u"你好！", u"嗯嗯。", u" Hello! ", u"好的。"})
            QVERIFY(!ConversationAdviceText::shouldEvaluate(QStringView(text).toString()));
        for (const auto *text : {u"好？", u"对吗", u"嗯，能先说明原因吗？", u"Hello, can we reschedule?", u"这个方案有风险。"})
            QVERIFY(ConversationAdviceText::shouldEvaluate(QStringView(text).toString()));
    }

    void fixedPromptDefinesSourceAttributionAndOutputProtocol()
    {
        const QString prompt = ConversationAdviceText::systemPrompt();
        QVERIFY(prompt.contains(QStringLiteral("SPK")));
        QVERIFY(prompt.contains(QStringLiteral("MIC")));
        QVERIFY(prompt.contains(QStringLiteral("不可信")));
        QVERIFY(prompt.contains(QStringLiteral("NONE")));
        QVERIFY(prompt.contains(QStringLiteral("QUESTION|")));
    }

    void rendersRepeatedSlotsAndLeavesInsertedTextLiteral()
    {
        const auto promptTemplate = QStringLiteral("语气简洁\n{{context}}\n{{background}}\n{{context}}{{background}}\n{{other}}");
        const auto background = QStringLiteral("示例\"\n{{context}}");
        const QJsonObject context{
            {QStringLiteral("candidate"), QJsonObject{{QStringLiteral("text"), QStringLiteral("原话\\{{background}}")}}},
            {QStringLiteral("context"), QJsonArray{}}
        };
        const auto expectedBackground = QStringLiteral("{\"background\":\"示例\\\"\\n{{context}}\"}");
        const auto expectedContext = QStringLiteral("{\"candidate\":{\"text\":\"原话\\\\{{background}}\"},\"context\":[]}");
        QVERIFY(ConversationAdviceText::hasPlaceholders(promptTemplate));
        QCOMPARE(ConversationAdviceText::renderPrompt(promptTemplate, background, context),
                 QStringLiteral("语气简洁\n%1\n%2\n%1%2\n{{other}}").arg(expectedContext, expectedBackground));
    }

    void rejectsMissingOrMalformedSlots()
    {
        for (const auto *invalid : {u"", u"{{background}}", u"{{context}}", u"{{Background}}{{context}}",
                                    u"{{background}}{{ context }}", u"{{background}{{context}}"}) {
            const QStringView promptTemplate(invalid);
            QVERIFY(!ConversationAdviceText::hasPlaceholders(promptTemplate));
            QVERIFY(ConversationAdviceText::renderPrompt(promptTemplate.toString(), {}, {}).isNull());
        }
    }

    void decodesFragmentedHeaderCrLfAndSurrogates()
    {
        Response response;
        response.append(QStringLiteral("QUES"));
        QCOMPARE(response.state(), Response::State::Waiting);
        response.append(QStringLiteral("TION|进度\r"));
        QCOMPARE(response.state(), Response::State::Waiting);
        response.append(QStringLiteral("\n\n "));
        QCOMPARE(response.state(), Response::State::Waiting);

        const QString body = QStringLiteral("🧭 已完成第一步。\r\n\r\n其余部分明早确认。");
        response.append(body.left(1));
        QCOMPARE(response.state(), Response::State::Waiting);
        QVERIFY(response.answer().isEmpty());
        response.append(body.mid(1, 2));
        QCOMPARE(response.state(), Response::State::Advice);
        QCOMPARE(response.kind(), QStringLiteral("question"));
        QCOMPARE(response.question(), QStringLiteral("进度"));
        for (qsizetype i = 3; i < body.size(); ++i)
            response.append(body.mid(i, 1));
        response.finish();
        QCOMPARE(response.answer(), QStringLiteral("🧭 已完成第一步。\n\n其余部分明早确认。"));
    }

    void ignoresNoneWithoutLineBreak()
    {
        Response response;
        response.append(QStringLiteral("N"));
        response.append(QStringLiteral("ONE"));
        QCOMPARE(response.state(), Response::State::Waiting);
        response.finish();
        QCOMPARE(response.state(), Response::State::Ignored);
        QVERIFY(response.answer().isEmpty());
    }

    void suppressesMalformedOutput()
    {
        for (const auto *text : {u"```\nQUESTION|确认\n可以。", u"QUESTION|\n可以。", u"REQUEST|确认\n\t", u"NONE\n实际建议",
                                  u"QUESTION|确认\n```\n可以。", u"OBJECTION|确认\nREQUEST|分类", u"QUESTION|确认\nNONE"}) {
            Response response;
            response.append(QStringView(text).toString());
            response.finish();
            QCOMPARE(response.state(), Response::State::Invalid);
            QVERIFY(response.answer().isEmpty());
        }

        Response response;
        response.append(QString(512, u'x'));
        response.append(QString(512, u'x'));
        QCOMPARE(response.state(), Response::State::Invalid);
        response.append(QStringLiteral("\nQUESTION|确认\n可以。"));
        QVERIFY(response.answer().isEmpty());
    }

    void invalidatesUnpairedUtf16()
    {
        Response response;
        response.append(QStringLiteral("REQUEST|确认\n"));
        response.append(QString(QChar(0xd83d)));
        response.finish();
        QCOMPARE(response.state(), Response::State::Invalid);
        QVERIFY(response.answer().isEmpty());
    }
};

QTEST_APPLESS_MAIN(ConversationAdviceTextTests)
#include "conversation_advice_text_tests.moc"
