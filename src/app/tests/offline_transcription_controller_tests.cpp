#include "offline_transcription_controller.h"
#include <offline_transcription_text.h>
#include <QSettings>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class OfflineTranscriptionControllerTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("ChunfeOfflineTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Controller"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void legacyRuntimeSettingIsIgnored() {
        QSettings settings;
        settings.setValue(QStringLiteral("offlineTranscription/runtimePath"), QStringLiteral("/external/old-engine.dylib"));
        settings.setValue(QStringLiteral("offlineTranscription/modelPath"), QStringLiteral("/models/existing.gguf"));
        settings.sync();
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        QVERIFY(c.m_runtimePath != QStringLiteral("/external/old-engine.dylib"));
        QCOMPARE(c.modelPath(), QStringLiteral("/models/existing.gguf"));
        c.saveModelPath(QStringLiteral(" /models/new.gguf "));
        QCOMPARE(c.modelPath(), QStringLiteral("/models/new.gguf"));
        QVERIFY(!QSettings().contains(QStringLiteral("offlineTranscription/runtimePath")));
    }
    void missingBundledRuntimeReportsInstallationFailure() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile model(dir.filePath(QStringLiteral("model.gguf")));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.close();
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        c.saveModelPath(model.fileName());
        c.m_runtimePath = dir.filePath(QStringLiteral("missing-runtime"));
        c.loadEngine();
        QCOMPARE(c.engineStatus(), QStringLiteral("error"));
        QVERIFY(c.engineError().contains(QStringLiteral("重新安装")));
        c.open();
        QVERIFY(!c.opened());
    }
    void rollingDraftRevisesWordsWithoutChangingCapsuleId() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        c.applyResult(1, {{}, QStringLiteral("我们今天一。"), true}, false);
        QCOMPARE(c.lines().size(), 1);
        const auto micId = c.lines()[0].toMap().value("id");
        c.applyResult(2, {{}, QStringLiteral("System audio."), true}, false);
        const auto spkDraft = c.lines()[1];

        c.applyResult(1, {{}, QStringLiteral("我们今天一起讨论项目。"), true}, false);
        QCOMPARE(c.lines().size(), 2);
        QCOMPARE(c.lines()[0].toMap().value("id"), micId);
        QCOMPARE(c.lines()[0].toMap().value("text").toString(), QStringLiteral("我们今天一起讨论项目。"));
        QVERIFY(!c.lines()[0].toMap().value("final").toBool());
        QCOMPARE(c.lines()[1], spkDraft);

        c.applyResult(1, {}, false);
        QCOMPARE(c.lines()[0].toMap().value("text").toString(), QStringLiteral("我们今天一起讨论项目。"));
    }
    void confirmedSentencesFinalizeOnceAndInOrder() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        c.applyResult(1, {{}, QStringLiteral("讨论项目。"), true}, false);
        const auto firstId = c.lines()[0].toMap().value("id");
        c.applyResult(2, {{}, QStringLiteral("System audio."), true}, false);
        const auto spkDraft = c.lines()[1];
        const QString first = QStringLiteral("讨论项目的下一步安排。");
        const QString second = QStringLiteral("明天下午三点开会。");
        c.applyResult(1, {{first, second}, QStringLiteral("谢谢。"), true}, false);

        QCOMPARE(c.lines().size(), 4);
        QCOMPARE(c.lines()[0].toMap().value("id"), firstId);
        QCOMPARE(c.lines()[0].toMap().value("text").toString(), first);
        QVERIFY(c.lines()[0].toMap().value("final").toBool());
        QCOMPARE(c.lines()[1], spkDraft);
        QCOMPARE(c.lines()[2].toMap().value("text").toString(), second);
        QVERIFY(c.lines()[2].toMap().value("final").toBool());
        QCOMPARE(c.lines()[3].toMap().value("text").toString(), QStringLiteral("谢谢。"));
        QVERIFY(!c.lines()[3].toMap().value("final").toBool());
        const auto finalizedFirst = c.lines()[0];
        const auto finalizedSecond = c.lines()[2];
        const auto latestId = c.lines()[3].toMap().value("id");

        c.applyResult(1, {{}, QStringLiteral("非常感谢。"), true}, false);
        QCOMPARE(c.lines().size(), 4);
        QCOMPARE(c.lines()[0], finalizedFirst);
        QCOMPARE(c.lines()[1], spkDraft);
        QCOMPARE(c.lines()[2], finalizedSecond);
        QCOMPARE(c.lines()[3].toMap().value("id"), latestId);
        QCOMPARE(c.lines()[3].toMap().value("text").toString(), QStringLiteral("非常感谢。"));
    }
    void sourceFinalKeepsDraftAndStartsNewCapsule() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        c.applyResult(2, {{}, QStringLiteral("Hello"), true}, false);
        const auto spkDraft = c.lines()[0];
        c.applyResult(1, {{}, QStringLiteral("大家好。"), true}, false);
        const auto micId = c.lines()[1].toMap().value("id");
        c.applyResult(1, {}, true);
        QCOMPARE(c.lines().size(), 2);
        const auto firstMic = c.lines()[1];
        QVERIFY(firstMic.toMap().value("final").toBool());
        QCOMPARE(firstMic.toMap().value("id"), micId);
        QCOMPARE(firstMic.toMap().value("text").toString(), QStringLiteral("大家好。"));
        c.applyResult(1, {}, true);
        QCOMPARE(c.lines().size(), 2);

        c.applyResult(1, {{}, QStringLiteral("我们继续。"), true}, false);
        QCOMPARE(c.lines().size(), 3);
        QCOMPARE(c.lines()[0], spkDraft);
        QCOMPARE(c.lines()[1], firstMic);
        const auto nextMic = c.lines()[2].toMap();
        QVERIFY(nextMic.value("id") != micId);
        QCOMPARE(nextMic.value("source").toString(), QStringLiteral("mic"));
        QCOMPARE(nextMic.value("text").toString(), QStringLiteral("我们继续。"));
        QVERIFY(!nextMic.value("final").toBool());
    }
    void incompleteTailDoesNotInvalidateConfirmedSentence() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        const QString finished = QStringLiteral("这是第一句。");
        const QString unfinished = QStringLiteral("尾音未完成");
        c.applyResult(1, {{}, QStringLiteral("这是第一句。尾音未"), true}, false);
        c.applyResult(1, {{finished}, unfinished, true}, true, true);
        const auto lines = c.lines();
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].toMap().value("text").toString(), finished);
        QVERIFY(lines[0].toMap().value("final").toBool());
        QVERIFY(!lines[0].toMap().value("incomplete").toBool());
        QCOMPARE(lines[1].toMap().value("text").toString(), unfinished);
        QVERIFY(!lines[1].toMap().value("final").toBool());
        QVERIFY(lines[1].toMap().value("incomplete").toBool());
    }
    void emptyDraftRevisionRemovesOnlyItsSource() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        c.applyResult(1, {{}, QStringLiteral("待确认"), true}, false);
        c.applyResult(2, {{}, QStringLiteral("System audio"), true}, false);
        const auto spkDraft = c.lines()[1];
        c.applyResult(1, {{}, {}, true}, false);
        c.applyResult(1, {}, true);
        QCOMPARE(c.lines(), QVariantList{spkDraft});
    }
    void sentenceBoundariesKeepNumbersAbbreviationsAndClosingQuotes() {
        const auto text = QStringLiteral("Dr. Smith said \"The U.S. team won by 3.14 kg.\" 她问：“真的吗？！”我点头。");
        const auto ends = offlineSentenceEnds(text);
        QCOMPARE(ends.size(), 3);
        QCOMPARE(text.left(ends[0]), QStringLiteral("Dr. Smith said \"The U.S. team won by 3.14 kg.\""));
        QCOMPARE(text.mid(ends[0], ends[1] - ends[0]).trimmed(), QStringLiteral("她问：“真的吗？！”"));
        QCOMPARE(text.mid(ends[1], ends[2] - ends[1]), QStringLiteral("我点头。"));
    }
    void alignmentUsesAudioShiftForRepeatedWords() {
        const auto result = alignOfflineTranscript(u"go go go go go go go go", u"Go go go go go go go go", 0.25);
        QCOMPARE(result.matches.size(), 6);
        QCOMPARE(result.matches.first().previousIndex, 2);
        QCOMPARE(result.matches.first().currentIndex, 0);
        QCOMPARE(result.matches.first().previousStart, 6);
        QCOMPARE(result.matches.first().currentStart, 0);
        QVERIFY(alignOfflineTranscript(u"blue fox", u"large cat", 0.25).matches.isEmpty());
    }
    void failedSettingsWriteIsVisibleAndKeepsDraftModel() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QFile blocker(dir.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, blocker.fileName());
        {
            AudioPiplineManager manager;
            OfflineTranscriptionController c(manager);
            c.saveModelPath(QStringLiteral("/model"));
            QCOMPARE(c.modelPath(), QStringLiteral("/model"));
            QVERIFY(!c.engineError().isEmpty());
        }
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void modelPathPersistsAndInvalidModelDoesNotStartEngine() {
        AudioPiplineManager manager;
        {
            OfflineTranscriptionController c(manager);
            c.saveModelPath(QStringLiteral("/not-a-model"));
            c.loadEngine();
            QCOMPARE(c.engineStatus(), QStringLiteral("error"));
            QVERIFY(!c.engineError().isEmpty());
            c.open();
            QVERIFY(!c.opened());
        }
        OfflineTranscriptionController c(manager);
        QCOMPARE(c.modelPath(), QStringLiteral("/not-a-model"));
        QCOMPARE(c.engineStatus(), QStringLiteral("stopped"));
    }
private:
    QTemporaryDir m_settings;
};
QTEST_GUILESS_MAIN(OfflineTranscriptionControllerTests)
#include "offline_transcription_controller_tests.moc"
