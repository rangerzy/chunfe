#include "offline_transcription_engine.h"
#include <QFile>
#include <QLibrary>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <cstdint>

namespace {
constexpr qsizetype WindowFrames = 16000;

QByteArray pcm(qsizetype frames, qint16 sample) {
    QByteArray data(frames * sizeof(sample), Qt::Uninitialized);
    for (qsizetype i = 0; i < data.size(); i += sizeof(sample))
        qToLittleEndian(sample, data.data() + i);
    return data;
}
}

class EngineTests : public QObject {
    Q_OBJECT
private slots:
    void twoSourcesUseOneModelAndOneSession() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto modelLoads = reinterpret_cast<int (*)()>(runtime.resolve("offline_fake_model_load_count"));
        auto sessions = reinterpret_cast<int (*)()>(runtime.resolve("offline_fake_session_create_count"));
        auto starts = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_start_count"));
        QVERIFY(reset && modelLoads && sessions && starts);
        reset();
        OfflineTranscriptionEngine engine;
        QString error;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.start(2, &error), qPrintable(error));
        QCOMPARE(modelLoads(), 1);
        QCOMPARE(sessions(), 1);
        QCOMPARE(starts(0), 0);
    }
    void everyWindowLimitsOutput() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto tokenLimit = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_max_tokens"));
        auto starts = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_start_count"));
        auto finishes = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_finish_count"));
        QVERIFY(reset && tokenLimit && starts && finishes);
        reset();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.start(2, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
        QCOMPARE(tokenLimit(0), 64);
        QVERIFY2(engine.push(2, pcm(WindowFrames, -1000), &result, &error), qPrintable(error));
        QCOMPARE(tokenLimit(0), 64);
        engine.reset(1);
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(300, 1000), &result, &error), qPrintable(error));
        QVERIFY2(engine.finish(1, &result, &error), qPrintable(error));
        QCOMPARE(tokenLimit(0), 64);
        QVERIFY2(engine.finish(2, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("spk"));
        QCOMPARE(starts(0), 3);
        QCOMPARE(finishes(0), 3);
    }
    void decodeWindowsIncludePreviousAudioWithoutGrowingUnbounded() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto frames = reinterpret_cast<uint64_t (*)(int)>(runtime.resolve("offline_fake_frame_count"));
        QVERIFY(reset && frames);
        reset();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        for (int second = 1; second <= 6; ++second) {
            const auto before = frames(0);
            QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
            QCOMPARE(frames(0) - before, uint64_t(std::min(second, 4) * WindowFrames));
            QCOMPARE(result.draft, QStringLiteral("mic ").repeated(second).trimmed());
            QVERIFY(result.sentences.isEmpty());
        }
    }
    void laterContextRevisesTheDraftAndConfirmsSentences() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        qputenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION", "1");
        const auto restoreEnvironment = qScopeGuard([] { qunsetenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION"); });

        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));

        QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
        QVERIFY(result.updated);
        QCOMPARE(result.draft, QStringLiteral("We arrived."));
        QVERIFY(result.sentences.isEmpty());
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1100), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("We arrived early."));
        QVERIFY(result.sentences.isEmpty());
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1200), &result, &error), qPrintable(error));
        QCOMPARE(result.sentences, QStringList{QStringLiteral("We arrived early.")});
        QCOMPARE(result.draft, QStringLiteral("The room."));

        QVERIFY2(engine.push(1, pcm(WindowFrames, 1300), &result, &error), qPrintable(error));
        QVERIFY(result.sentences.isEmpty());
        QCOMPARE(result.draft, QStringLiteral("The room was quiet."));
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1400), &result, &error), qPrintable(error));
        QCOMPARE(result.sentences, QStringList{QStringLiteral("The room was quiet.")});
        QCOMPARE(result.draft, QStringLiteral("Then."));

        QVERIFY2(engine.push(1, pcm(WindowFrames, 1500), &result, &error), qPrintable(error));
        QVERIFY(result.sentences.isEmpty());
        QCOMPARE(result.draft, QStringLiteral("Then we left."));
        QVERIFY2(engine.push(1, pcm(8000, 1600), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        QVERIFY2(engine.finish(1, &result, &error), qPrintable(error));
        QVERIFY(result.sentences.isEmpty());
        QCOMPARE(result.draft, QStringLiteral("Then we left together."));

        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QByteArray combined;
        for (const qint16 amplitude : {1000, 1100, 1200, 1300, 1400})
            combined += pcm(WindowFrames, amplitude);
        QVERIFY2(engine.push(1, combined, &result, &error), qPrintable(error));
        const QStringList expectedSentences{QStringLiteral("We arrived early."), QStringLiteral("The room was quiet.")};
        QCOMPARE(result.sentences, expectedSentences);
        QCOMPARE(result.draft, QStringLiteral("Then."));
    }
    void rejectedOutputLimitDoesNotStartStream() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto starts = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_start_count"));
        QVERIFY(reset && starts);
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        reset();
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        qputenv("OFFLINE_FAKE_TOKEN_OPTION_FAILURE", "1");
        const bool ok = engine.push(1, pcm(WindowFrames, 1000), &result, &error);
        qunsetenv("OFFLINE_FAKE_TOKEN_OPTION_FAILURE");
        QVERIFY(!ok);
        QVERIFY(error.contains(QStringLiteral("输出上限")));
        QCOMPARE(starts(0), 0);
    }
    void sentenceBoundarySurvivesWhenItsAudioLeavesTheContext() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        qputenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION", "1");
        const auto restoreEnvironment = qScopeGuard([] { qunsetenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION"); });

        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        for (const qint16 amplitude : {1700, 1800, 1900, 2000}) {
            QVERIFY2(engine.push(1, pcm(WindowFrames, amplitude), &result, &error), qPrintable(error));
            QVERIFY(result.sentences.isEmpty());
        }
        QCOMPARE(result.draft, QStringLiteral("First line. Here comes another phrase and it ends."));

        QVERIFY2(engine.push(1, pcm(WindowFrames, 2100), &result, &error), qPrintable(error));
        QCOMPARE(result.sentences, QStringList{QStringLiteral("First line.")});
        QCOMPARE(result.draft, QStringLiteral("Here comes another phrase and it ends right here."));
    }
    void emptyFallbackKeepsTextWhenTheNextSpeechArrives() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.close();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(4 * WindowFrames, 0), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic"));
        QVERIFY(result.sentences.isEmpty());

        QVERIFY2(engine.push(1, pcm(WindowFrames, -1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic spk"));
        QVERIFY(result.sentences.isEmpty());
    }
    void invalidPathsAreRejected() {
        OfflineTranscriptionEngine engine;
        QString error;
        QVERIFY(!engine.load(QStringLiteral("/missing/runtime"), QStringLiteral("/missing/model"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!engine.isLoaded());
    }
    void realRuntimeRejectsInvalidModel() {
        const QString runtime = qEnvironmentVariable("CHUNFE_REAL_AUDIOCPP_RUNTIME");
        if (runtime.isEmpty()) QSKIP("Set CHUNFE_REAL_AUDIOCPP_RUNTIME to a real audio.cpp library for this optional smoke test");
        QTemporaryDir dir;
        QFile model(dir.filePath("invalid.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("not a model"); model.close();
        OfflineTranscriptionEngine engine;
        QString error;
        QVERIFY(!engine.load(runtime, model.fileName(), &error));
        QVERIFY2(error.contains(QStringLiteral("加载转写模型失败")), qPrintable(error));
        QVERIFY(!engine.isLoaded());
    }
    void invalidAbiAndGpuErrorsAreReported() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        OfflineTranscriptionEngine engine;
        QString error;
        qputenv("OFFLINE_FAKE_BAD_ABI", "1");
        bool ok = engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error);
        qunsetenv("OFFLINE_FAKE_BAD_ABI");
        QVERIFY(!ok);
        QVERIFY(error.contains("ABI"));
        QVERIFY(!engine.isLoaded());
        qputenv("OFFLINE_FAKE_GPU_FAILURE", "1");
        ok = engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error);
        qunsetenv("OFFLINE_FAKE_GPU_FAILURE");
        QVERIFY(!ok);
        QVERIFY(error.contains("GPU"));
        QVERIFY(!engine.isLoaded());
    }
    void largeInputPreservesAllWindows() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto starts = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_start_count"));
        auto finishes = reinterpret_cast<int (*)(int)>(runtime.resolve("offline_fake_finish_count"));
        auto frames = reinterpret_cast<uint64_t (*)(int)>(runtime.resolve("offline_fake_frame_count"));
        QVERIFY(reset && starts && finishes && frames);
        reset();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        const auto silence = pcm(WindowFrames, 0);
        const auto speech = silence + pcm(WindowFrames, 1000) + silence + pcm(WindowFrames, -1000);
        QVERIFY2(engine.push(1, speech, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic spk"));
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic spk mic"));
        QVERIFY2(engine.finish(1, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic spk mic"));
        QCOMPARE(starts(0), 5);
        QCOMPARE(finishes(0), 5);
        QCOMPARE(frames(0), uint64_t((1 + 2 + 3 + 4 + 4) * WindowFrames));
    }
    void streamsAreIndependent() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        QLibrary runtime(QString::fromUtf8(FAKE_RUNTIME_PATH));
        QVERIFY(runtime.load());
        auto reset = reinterpret_cast<void (*)()>(runtime.resolve("offline_fake_reset_counters"));
        auto frames = reinterpret_cast<uint64_t (*)(int)>(runtime.resolve("offline_fake_frame_count"));
        QVERIFY(reset && frames);
        reset();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY(!engine.start(3, &error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.start(2, &error), qPrintable(error));
        QVERIFY(!engine.push(1, QByteArray(1, '\0'), &result, &error));
        QVERIFY2(engine.push(1, pcm(10000, 1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        QVERIFY2(engine.push(2, pcm(7000, -1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        QVERIFY2(engine.push(1, pcm(7000, 1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic"));
        QVERIFY2(engine.push(2, pcm(11000, -1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("spk"));
        QCOMPARE(frames(0), uint64_t(32000));
        QVERIFY2(engine.finish(1, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic mic tail"));
        QCOMPARE(frames(0), uint64_t(49000));
        QVERIFY2(engine.finish(2, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("spk spk tail"));
        QCOMPARE(frames(0), uint64_t(67000));
    }
    void resetDiscardsOnlyOneSourcesBufferedAudio() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.start(2, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(9000, 1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        QVERIFY2(engine.push(2, pcm(8000, -1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        engine.reset(1);
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(7100, 1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);
        QVERIFY2(engine.push(2, pcm(8000, -1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("spk"));
        QVERIFY2(engine.finish(1, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic tail"));
    }
    void failedWindowLeavesOtherSourceTailAvailable() {
        QTemporaryDir dir;
        QFile model(dir.filePath("model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("model"); model.close();
        OfflineTranscriptionEngine engine;
        QString error;
        OfflineTranscriptionEngine::Result result;
        QVERIFY2(engine.load(QString::fromUtf8(FAKE_RUNTIME_PATH), model.fileName(), &error), qPrintable(error));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.start(2, &error), qPrintable(error));
        QVERIFY2(engine.push(2, pcm(2300, -1000), &result, &error), qPrintable(error));
        QVERIFY(!result.updated);

        qputenv("OFFLINE_FAKE_PUSH_FAILURE", "1");
        const bool ok = engine.push(1, pcm(WindowFrames, 1000), &result, &error);
        qunsetenv("OFFLINE_FAKE_PUSH_FAILURE");
        QVERIFY(!ok);
        QVERIFY(error.contains(QStringLiteral("转写失败")));
        QVERIFY(!result.updated);
        engine.reset(1);

        QVERIFY2(engine.finish(2, &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("spk tail"));
        QVERIFY2(engine.start(1, &error), qPrintable(error));
        QVERIFY2(engine.push(1, pcm(WindowFrames, 1000), &result, &error), qPrintable(error));
        QCOMPARE(result.draft, QStringLiteral("mic"));
    }
};
QTEST_GUILESS_MAIN(EngineTests)
#include "engine_tests.moc"
