#include "offline_transcription_controller.h"
#include <QFile>
#include <QMetaObject>
#include <QLibrary>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <algorithm>

using namespace AudioPipline;

// Exercise the real controller and dynamically loaded fake C ABI. Inject the
// capture manager's signals so the tests need no devices or microphone permission.
class OfflineTranscriptionLifecycleTests : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_settings;
    QString m_configFilePath;
    QTemporaryDir m_modelDir;
    QString m_model;
    QLibrary m_fake{QString::fromUtf8(FAKE_RUNTIME_PATH)};
    using ResetCounters = void (*)();
    using Counter = int (*)(int);
    using FrameCounter = uint64_t (*)(int);
    ResetCounters resetCounters = nullptr;
    Counter pushCount = nullptr;
    Counter finishCount = nullptr;
    Counter startCount = nullptr;
    FrameCounter frameCount = nullptr;
    CaptureId startCaptureSignals(OfflineTranscriptionController &c, bool includeSpk = true) {
        c.setDevices(QStringLiteral("mic"), includeSpk ? QStringLiteral("spk") : QString{});
        c.m_opened = true;
        c.startTest();
        c.m_status = QStringLiteral("running");
        if (!workerBarrier(c)) return {};
        QCoreApplication::processEvents();
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.m_captureId = id;
        return id;
    }
    CaptureId startConversationSignals(OfflineTranscriptionController &c, AudioPiplineManager &manager) {
        c.setDevices(QStringLiteral("mic"), QStringLiteral("spk"));
        if (!c.startConversation()) return {};
        c.m_captureId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.m_attemptedSources = 3;
        emit manager.captureChanged({c.m_captureId, CaptureState::Running});
        if (!workerBarrier(c)) return {};
        QCoreApplication::processEvents();
        return c.m_captureId;
    }
    void emitDualAudio(AudioPiplineManager &manager, const CaptureId &id, bool voice, std::uint32_t epoch = 0) {
        QByteArray pcm(6400, '\0');
        if (voice) for (int i = 0; i < pcm.size(); i += 2)
            qToLittleEndian<qint16>((i / 2) % 2 ? 1000 : -1000, reinterpret_cast<uchar *>(pcm.data() + i));
        emit manager.pcmReady(id, epoch, AudioSource::Mic, {pcm});
        emit manager.pcmReady(id, epoch, AudioSource::Spk, {pcm});
    }
    void closeInjectedCapture(OfflineTranscriptionController &c) {
        c.m_captureId.clear();
        c.close();
    }
    void drainInjectedCapture(OfflineTranscriptionController &c, AudioPiplineManager &manager,
                              const CaptureId &id, std::uint64_t micTrailingDrops = 0) {
        c.m_status = QStringLiteral("stopping");
        emit manager.pcmEnded(id, 0, AudioSource::Mic, micTrailingDrops);
        emit manager.pcmEnded(id, 0, AudioSource::Spk, 0);
        CaptureSnapshot snapshot;
        snapshot.captureId = id;
        emit manager.captureChanged(snapshot);
    }
    bool load(OfflineTranscriptionController &c) {
        c.m_runtimePath = QString::fromUtf8(FAKE_RUNTIME_PATH);
        c.m_vadModelPath = m_model;
        c.loadEngine(m_model);
        QElapsedTimer timer; timer.start();
        while (c.engineStatus() == QStringLiteral("starting") && timer.elapsed() < 5000)
            QTest::qWait(10);
        return c.engineStatus() == QStringLiteral("ready");
    }
    bool workerBarrier(OfflineTranscriptionController &c) {
        return QMetaObject::invokeMethod(c.m_workerContext, [] {}, Qt::BlockingQueuedConnection);
    }
    static QByteArray readPcmFixture(const QString &path) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return {};
        const QByteArray wav = input.readAll();
        if (wav.size() < 12 || wav.mid(0, 4) != "RIFF" || wav.mid(8, 4) != "WAVE") return {};
        QByteArray pcm;
        bool validFormat = false;
        for (qsizetype pos = 12; pos + 8 <= wav.size();) {
            const QByteArray kind = wav.mid(pos, 4);
            const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wav.constData() + pos + 4));
            pos += 8;
            if (size > quint64(wav.size() - pos)) return {};
            if (kind == "fmt " && size >= 16) {
                const auto *p = reinterpret_cast<const uchar *>(wav.constData() + pos);
                validFormat = qFromLittleEndian<quint16>(p) == 1 && qFromLittleEndian<quint16>(p + 2) == 1 &&
                    qFromLittleEndian<quint32>(p + 4) == 16000 && qFromLittleEndian<quint16>(p + 14) == 16;
            } else if (kind == "data") pcm = wav.mid(pos, size);
            pos += size + (size & 1u);
        }
        return validFormat ? pcm : QByteArray{};
    }
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());
        QVERIFY(m_modelDir.isValid());
        m_configFilePath = m_settings.filePath(QStringLiteral("config.ini"));
        m_model = m_modelDir.filePath(QStringLiteral("model.gguf"));
        QFile model(m_model); QVERIFY(model.open(QIODevice::WriteOnly));
        QCOMPARE(model.write("test model"), qint64(10));
        QVERIFY(m_fake.load());
        resetCounters = reinterpret_cast<ResetCounters>(m_fake.resolve("offline_fake_reset_counters"));
        pushCount = reinterpret_cast<Counter>(m_fake.resolve("offline_fake_push_count"));
        finishCount = reinterpret_cast<Counter>(m_fake.resolve("offline_fake_finish_count"));
        startCount = reinterpret_cast<Counter>(m_fake.resolve("offline_fake_start_count"));
        frameCount = reinterpret_cast<FrameCounter>(m_fake.resolve("offline_fake_frame_count"));
        QVERIFY(resetCounters && pushCount && finishCount && startCount && frameCount);
    }
    void remembersOnlySuccessfullyLoadedModels() {
        const QString modelKey = QStringLiteral("offlineTranscription/modelPath");
        const QString runtimeKey = QStringLiteral("offlineTranscription/runtimePath");
        const QString modelIdKey = QStringLiteral("modelConnection/modelId");
        const QString modelId = QStringLiteral("saved-model-id");
        const QString previousPath = QStringLiteral("/models/previous.gguf");
        QSettings settings(m_configFilePath, QSettings::IniFormat);
        settings.setValue(modelKey, previousPath);
        settings.setValue(runtimeKey, QStringLiteral("/old/runtime"));
        settings.setValue(modelIdKey, modelId);
        settings.sync();

        AudioPiplineManager manager;
        {
            OfflineTranscriptionController c(manager, m_configFilePath);
            QCOMPARE(c.modelPath(), previousPath);
            QCOMPARE(c.engineStatus(), QStringLiteral("stopped"));
            QVERIFY(c.attemptedModelPath().isEmpty());
            c.m_vadModelPath = m_model;

            // An existing but invalid runtime reaches the asynchronous failure path.
            c.m_runtimePath = m_model;
            QVERIFY(QMetaObject::invokeMethod(&c, "loadEngine", m_model));
            QTRY_COMPARE(c.engineStatus(), QStringLiteral("error"));
            QVERIFY(!c.engineError().isEmpty());
            QCOMPARE(c.attemptedModelPath(), m_model);
            QCOMPARE(c.modelPath(), previousPath);
            QCOMPARE(QSettings(m_configFilePath, QSettings::IniFormat).value(modelKey).toString(), previousPath);

            c.m_runtimePath = QString::fromUtf8(FAKE_RUNTIME_PATH);
            QVERIFY(QMetaObject::invokeMethod(&c, "loadEngine", QStringLiteral(" %1 ").arg(m_model)));
            QTRY_COMPARE(c.engineStatus(), QStringLiteral("ready"));
            QCOMPARE(c.modelPath(), m_model);
            QSettings persisted(m_configFilePath, QSettings::IniFormat);
            QCOMPARE(persisted.value(modelKey).toString(), m_model);
            QCOMPARE(persisted.value(runtimeKey).toString(), QStringLiteral("/old/runtime"));
            QCOMPARE(persisted.value(modelIdKey).toString(), modelId);

            c.unloadEngine();
            QCOMPARE(c.engineStatus(), QStringLiteral("stopping"));
            QTRY_COMPARE(c.engineStatus(), QStringLiteral("stopped"));
            QCOMPARE(c.modelPath(), m_model);
        }
        OfflineTranscriptionController reopened(manager, m_configFilePath);
        QCOMPARE(reopened.modelPath(), m_model);
        QVERIFY(reopened.attemptedModelPath().isEmpty());
        QCOMPARE(reopened.engineStatus(), QStringLiteral("stopped"));
    }
    void settingsWriteFailureKeepsTheEngineReadyAndReportsTheError() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile blocker(dir.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, dir.filePath(QStringLiteral("file/config.ini")));
        QVERIFY2(load(c), qPrintable(c.engineError()));
        QCOMPARE(c.modelPath(), m_model);
        QVERIFY(!c.engineError().isEmpty());
    }
    void conversationAbortKeepsFullTranscriptAndClearsItOnNextStart() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QSignalSpy finished(&c, &OfflineTranscriptionController::conversationFinished);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startConversationSignals(c, manager);
        QVERIFY(!id.isEmpty());
        QVERIFY(c.conversationActive());
        c.open();
        QVERIFY(!c.opened());
        for (int i = 0; i < 50; ++i)
            c.applyResult(2, {{QString::number(i)}, {}, true}, false);
        c.applyResult(1, {{}, QStringLiteral("mic draft"), true}, false);
        QCOMPARE(c.conversationLines().size(), 40);

        c.abortConversation();
        QVERIFY(!c.conversationActive());
        QCOMPARE(finished.size(), 1);
        const auto transcript = finished.first().first().toList();
        QCOMPARE(c.lines(), transcript);
        QCOMPARE(transcript.size(), 51);
        QCOMPARE(transcript.first().toMap().value("text").toString(), QStringLiteral("0"));
        QVERIFY(transcript.last().toMap().value("incomplete").toBool());
        c.abortConversation();
        QCOMPARE(finished.size(), 1);
        emit manager.captureChanged({id, CaptureState::Idle});

        QVERIFY(!startConversationSignals(c, manager).isEmpty());
        emitDualAudio(manager, id, true);
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QVERIFY(c.lines().isEmpty());
        c.abortConversation();
        QCOMPARE(finished.size(), 2);
    }
    void conversationStopDrainsBothSourcesAfterCaptureEnds() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QSignalSpy finished(&c, &OfflineTranscriptionController::conversationFinished);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startConversationSignals(c, manager);
        QVERIFY(!id.isEmpty());
        resetCounters();
        for (int n = 0; n < 4; ++n) emitDualAudio(manager, id, true);
        c.stopConversation();
        QCOMPARE(c.status(), QStringLiteral("stopping"));
        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE(finished.size(), 1);
        const auto lines = finished.first().first().toList();
        QVERIFY(!c.conversationActive());
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        QCOMPARE(finishCount(0), 2);
        QCOMPARE(lines.size(), 2);
        for (auto line : lines) QVERIFY(line.toMap().value("final").toBool());
        QCOMPARE(lines.first().toMap().value("source").toString(), QStringLiteral("mic"));
        QCOMPARE(lines.last().toMap().value("source").toString(), QStringLiteral("spk"));
    }
    void conversationPauseSealsAcceptedTailBeforeNextCaptureEpoch() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startConversationSignals(c, manager);
        QVERIFY(!id.isEmpty());
        resetCounters();
        for (int n = 0; n < 4; ++n) emitDualAudio(manager, id, true);

        c.m_status = QStringLiteral("pausing");
        emit manager.captureChanged({id, CaptureState::Paused});
        QCOMPARE(c.status(), QStringLiteral("paused"));
        emitDualAudio(manager, id, true);
        QTRY_COMPARE(c.lines().size(), 2);
        for (auto line : c.lines()) QVERIFY(line.toMap().value("final").toBool());

        c.m_status = QStringLiteral("resuming");
        emit manager.captureChanged({id, CaptureState::Running, 1});
        QCOMPARE(c.status(), QStringLiteral("running"));
        const auto pendingBytes = c.m_pendingBytes[0];
        emitDualAudio(manager, id, true); // Old epoch must stay sealed.
        QCOMPARE(c.m_pendingBytes[0], pendingBytes);
        for (int n = 0; n < 4; ++n) emitDualAudio(manager, id, true, 1);
        c.stopConversation();
        emit manager.pcmEnded(id, 1, AudioSource::Mic, 0);
        emit manager.pcmEnded(id, 1, AudioSource::Spk, 0);
        emit manager.captureChanged({id, CaptureState::Idle, 1});
        QTRY_COMPARE(c.status(), QStringLiteral("stopped"));
        QCOMPARE(c.lines().size(), 4);
        QCOMPARE(finishCount(0), 4);
    }
    void silentDualCaptureDoesNotInvokeInference() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        resetCounters();
        for (int n = 0; n < 15; ++n) emitDualAudio(manager, id, false);
        QVERIFY(workerBarrier(c)); QCoreApplication::processEvents();
        QCOMPARE(pushCount(0), 0);
        QCOMPARE(finishCount(0), 0);
        QVERIFY(c.lines().isEmpty());
        closeInjectedCapture(c);
    }
    void captureGapLogsDroppedAudioAndMarksOnlyItsSource() {
        qputenv("OFFLINE_FAKE_VAD_SCENARIO", "1");
        const auto restore = qScopeGuard([] { qunsetenv("OFFLINE_FAKE_VAD_SCENARIO"); });
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());

        constexpr int BeforeGapSamples = 31 * 512 + 101;
        constexpr int AfterGapSamples = 512;
        constexpr int SpkSamples = 52 * 512;
        constexpr int DroppedSamples = 160;
        const auto pcm = [](int samples, qint16 value) {
            QByteArray data(samples * 2, Qt::Uninitialized);
            for (int offset = 0; offset < data.size(); offset += 2)
                qToLittleEndian(value, reinterpret_cast<uchar *>(data.data() + offset));
            return data;
        };
        emit manager.pcmReady(id, 0, AudioSource::Spk, {pcm(SpkSamples, -1000)});
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QCOMPARE(c.lines().size(), 1);
        const auto spkDraft = c.lines().front().toMap();
        QCOMPARE(spkDraft.value(QStringLiteral("text")).toString(), QStringLiteral("spk"));
        resetCounters();

        QSemaphore entered, release;
        QMetaObject::invokeMethod(c.m_workerContext, [&] { entered.release(); release.acquire(); });
        if (!entered.tryAcquire(1, 3000)) {
            release.release(); workerBarrier(c); QFAIL("worker did not reach the queue barrier");
        }
        // At the gap, the first phrase has both queued PCM and VAD-held tail.
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm(BeforeGapSamples, 1000)});

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral("capture-gap .*source=MIC .*capsule=-1 .*dropped_frames=160 sample_rate=16000 dropped_ms=10")));
        const auto afterGapNs = qint64(BeforeGapSamples + DroppedSamples) * 1'000'000'000 / 16'000;
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm(AfterGapSamples, 2000), 2, afterGapNs, DroppedSamples});
        release.release();
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();

        QCOMPARE(frameCount(0), uint64_t(BeforeGapSamples));
        QCOMPARE(pushCount(0), 1);
        QCOMPARE(c.lines().size(), 2);
        QCOMPARE(c.lines().front().toMap(), spkDraft);
        const auto interruptedMic = c.lines().at(1).toMap();
        QCOMPARE(interruptedMic.value(QStringLiteral("text")).toString(), QStringLiteral("first tail"));
        QVERIFY(interruptedMic.value(QStringLiteral("incomplete")).toBool());
        QVERIFY(!interruptedMic.value(QStringLiteral("final")).toBool());

        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        QCOMPARE(frameCount(0), uint64_t(BeforeGapSamples + AfterGapSamples + SpkSamples));
        QCOMPARE(c.lines().size(), 3);
        QCOMPARE(c.lines().at(1).toMap(), interruptedMic);
        const auto finalSpk = c.lines().front().toMap();
        QCOMPARE(finalSpk.value(QStringLiteral("id")), spkDraft.value(QStringLiteral("id")));
        QCOMPARE(finalSpk.value(QStringLiteral("text")).toString(), QStringLiteral("spk spk tail"));
        QVERIFY(finalSpk.value(QStringLiteral("final")).toBool());
        QVERIFY(!finalSpk.value(QStringLiteral("incomplete")).toBool());
        const auto nextMic = c.lines().at(2).toMap();
        QVERIFY(nextMic.value(QStringLiteral("id")) != interruptedMic.value(QStringLiteral("id")));
        QCOMPARE(nextMic.value(QStringLiteral("text")).toString(), QStringLiteral("second tail"));
        QVERIFY(nextMic.value(QStringLiteral("final")).toBool());
        QVERIFY(!nextMic.value(QStringLiteral("incomplete")).toBool());
        c.close();
    }
    void closeSkipsQueuedInferenceBeforeExecution() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        resetCounters();
        QSemaphore entered, release;
        QMetaObject::invokeMethod(c.m_workerContext, [&] { entered.release(); release.acquire(); });
        if (!entered.tryAcquire(1, 3000)) {
            release.release(); workerBarrier(c); QFAIL("worker did not reach the queue barrier");
        }
        // Queue a complete window per source so missed cancellation would
        // invoke the shared native session.
        for (int n = 0; n < 9; ++n) emitDualAudio(manager, id, true);
        const bool queued = c.m_pendingBytes[0] > 0 && c.m_pendingBytes[1] > 0;
        closeInjectedCapture(c);
        release.release();
        QVERIFY(workerBarrier(c)); QCoreApplication::processEvents();
        QVERIFY(queued);
        QCOMPARE(pushCount(0), 0);
        QVERIFY(c.lines().isEmpty());
    }
    void timeoutSkipsQueuedInferenceAndFinalization() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        resetCounters();
        QSemaphore entered, release;
        QMetaObject::invokeMethod(c.m_workerContext, [&] { entered.release(); release.acquire(); });
        if (!entered.tryAcquire(1, 3000)) {
            release.release(); workerBarrier(c); QFAIL("worker did not reach the queue barrier");
        }
        for (int n = 0; n < 4; ++n) emitDualAudio(manager, id, true);
        const bool queued = c.m_pendingBytes[0] > 0 && c.m_pendingBytes[1] > 0;
        c.m_status = QStringLiteral("stopping");
        emit manager.pcmEnded(id, 0, AudioSource::Mic, 0);
        emit manager.pcmEnded(id, 0, AudioSource::Spk, 0);
        QMetaObject::invokeMethod(&c.m_stopTimer, "timeout", Qt::DirectConnection);
        release.release();
        QVERIFY(workerBarrier(c)); QCoreApplication::processEvents();
        QVERIFY(queued);
        QCOMPARE(pushCount(0), 0);
        QCOMPARE(finishCount(0), 0);
        QCOMPARE(c.m_pendingBytes[0], 0);
        QCOMPARE(c.m_pendingBytes[1], 0);
        closeInjectedCapture(c);
    }
    void timeoutRejectsLateAudioFromDrainingCapture() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        c.m_status = QStringLiteral("stopping");
        QMetaObject::invokeMethod(&c.m_stopTimer, "timeout", Qt::DirectConnection);
        QVERIFY(workerBarrier(c)); QCoreApplication::processEvents();
        resetCounters();
        QSignalSpy executions(&c, &OfflineTranscriptionController::inferenceCompleted);
        emitDualAudio(manager, id, true);
        QVERIFY(workerBarrier(c)); QCoreApplication::processEvents();
        QCOMPARE(executions.count(), 0);
        QCOMPARE(pushCount(0), 0);
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        QVERIFY(c.lines().isEmpty());
        closeInjectedCapture(c);
    }
    void failingLastSourceCompletesAnAlreadyDrainedStop() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        c.m_opened = true;
        c.m_status = QStringLiteral("stopping");
        c.m_captureEnded = true;
        c.m_finishing = c.m_finishingBySource[0] = 1;
        c.m_stopTimer.start(5000);
        c.failSource(1, QStringLiteral("source failed"));
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        QVERIFY(!c.m_stopTimer.isActive());
    }
    void stopDuringStreamStartupDoesNotRequestPermissionOrStartCapture() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        c.setDevices(QStringLiteral("test-mic"), {});
        c.open();
        QCOMPARE(c.status(), QStringLiteral("starting"));
        c.stop();
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QVERIFY(c.m_permissionRequest.isEmpty());
        QVERIFY(c.m_captureId.isEmpty());
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        c.close();
    }
    void closeDuringStreamStartupDiscardsLateCompletion() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        c.setDevices(QStringLiteral("test-mic"), {});
        c.open();
        c.close();
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QVERIFY(!c.opened());
        QVERIFY(c.m_permissionRequest.isEmpty());
        QVERIFY(c.m_captureId.isEmpty());
        QCOMPARE(c.status(), QStringLiteral("idle"));
        QCOMPARE(c.engineStatus(), QStringLiteral("ready"));
    }
    void timeoutMarksTailIncompleteAndRetainsEngine() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        c.m_opened = true;
        c.m_status = QStringLiteral("stopping");
        c.applyResult(1, {{}, QStringLiteral("未完成"), true}, false);
        const auto generation = c.m_generation;
        QVERIFY(QMetaObject::invokeMethod(&c.m_stopTimer, "timeout", Qt::DirectConnection));
        QCOMPARE(c.status(), QStringLiteral("stopped"));
        QVERIFY(c.m_generation > generation);
        QCOMPARE(c.lines().size(), 1);
        const auto line = c.lines().first().toMap();
        QVERIFY(line.value(QStringLiteral("incomplete")).toBool());
        QVERIFY(!line.value(QStringLiteral("final")).toBool());
        QCOMPARE(c.engineStatus(), QStringLiteral("ready"));
        c.close();
    }
    void twoPcmSourcesAndStreamEndProduceIndependentFinals() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        QByteArray pcm(1024, '\0');
        for (int n = 0; n < pcm.size(); n += 2)
            qToLittleEndian<qint16>(1000, reinterpret_cast<uchar *>(pcm.data() + n));
        QByteArray spkPcm(1024, '\0');
        for (int n = 0; n < spkPcm.size(); n += 2)
            qToLittleEndian<qint16>(-1000, reinterpret_cast<uchar *>(spkPcm.data() + n));
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm});
        emit manager.pcmReady(id, 0, AudioSource::Spk, {spkPcm});
        // After both VAD streams are active, Stop must preserve these fragments
        // even though they are shorter than one VAD frame.
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm.left(202)});
        emit manager.pcmReady(id, 0, AudioSource::Spk, {spkPcm.left(318)});
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QVERIFY(c.lines().isEmpty());
        resetCounters();
        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        QCOMPARE(c.lines().size(), 2);
        QMap<QString, QString> finalText;
        for (const auto &entry : c.lines()) {
            const auto line = entry.toMap();
            QVERIFY(line.value(QStringLiteral("final")).toBool());
            finalText.insert(line.value(QStringLiteral("source")).toString(), line.value(QStringLiteral("text")).toString());
        }
        QCOMPARE(finalText.value(QStringLiteral("mic")), QStringLiteral("mic tail"));
        QCOMPARE(finalText.value(QStringLiteral("spk")), QStringLiteral("spk tail"));
        QCOMPARE(frameCount(0), uint64_t((512 + 101) + (512 + 159)));
        c.close();
    }
    void lateCaptureSignalsDoNotConsumeNewCaptureTail() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        QByteArray pcm(1024, '\0');
        QByteArray unrelatedPcm(1024, '\0');
        for (int n = 0; n < pcm.size(); n += 2) {
            qToLittleEndian<qint16>(1000, reinterpret_cast<uchar *>(pcm.data() + n));
            qToLittleEndian<qint16>(-1000, reinterpret_cast<uchar *>(unrelatedPcm.data() + n));
        }
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm});
        emit manager.pcmReady(id, 1, AudioSource::Mic, {unrelatedPcm});
        emit manager.pcmReady(QStringLiteral("previous-capture"), 0, AudioSource::Mic, {unrelatedPcm});
        emit manager.pcmEnded(id, 1, AudioSource::Mic, 0);
        emit manager.pcmEnded(QStringLiteral("previous-capture"), 0, AudioSource::Mic, 0);
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QVERIFY(c.lines().isEmpty());
        emit manager.pcmEnded(id, 0, AudioSource::Mic, 0);
        emit manager.pcmReady(id, 0, AudioSource::Mic, {unrelatedPcm});
        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        QCOMPARE(c.lines().size(), 1);
        QCOMPARE(c.lines().at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("mic tail"));
        c.close();
    }
    void trailingCaptureGapKeepsAcceptedTail() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        QByteArray pcm(1024, '\0');
        for (int n = 0; n < pcm.size(); n += 2)
            qToLittleEndian<qint16>(1000, reinterpret_cast<uchar *>(pcm.data() + n));
        emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(
            QStringLiteral("capture-tail-gap .*source=MIC .*dropped_frames=160 sample_rate=16000 dropped_ms=10")));
        drainInjectedCapture(c, manager, id, 160);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        QCOMPARE(c.lines().size(), 1);
        const auto line = c.lines().at(0).toMap();
        QCOMPARE(line.value(QStringLiteral("text")).toString(), QStringLiteral("mic tail"));
        QVERIFY(line.value(QStringLiteral("incomplete")).toBool());
        c.close();
    }
    void stopDrainsAcceptedVadTailNearBacklogLimit() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c, false);
        QVERIFY(!id.isEmpty());
        QSignalSpy completed(&c, &OfflineTranscriptionController::inferenceCompleted);

        QSemaphore entered, release;
        QMetaObject::invokeMethod(c.m_workerContext, [&] { entered.release(); release.acquire(); });
        if (!entered.tryAcquire(1, 3000)) {
            release.release(); workerBarrier(c); QFAIL("worker did not reach the queue barrier");
        }
        constexpr int VadFrameSamples = 512;
        constexpr int CaptureFrames = 169; // 5.408 s, including 640 ms still held by VAD.
        QByteArray pcm(VadFrameSamples * 2, Qt::Uninitialized);
        for (int offset = 0; offset < pcm.size(); offset += 2)
            qToLittleEndian<qint16>(1000, reinterpret_cast<uchar *>(pcm.data() + offset));
        for (int n = 0; n < CaptureFrames; ++n)
            emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm, quint64(n), qint64(n) * 32'000'000});
        const bool admittedBeforeStop = c.errorMessage().isEmpty() && c.m_pendingBytes[0] > 4 * 32'000;
        drainInjectedCapture(c, manager, id);
        release.release();
        QVERIFY(workerBarrier(c));
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        QVERIFY(admittedBeforeStop);
        QVERIFY2(c.errorMessage().isEmpty(), qPrintable(c.errorMessage()));

        qint64 submittedBytes = 0;
        for (const auto &call : completed) submittedBytes += call.at(2).toInt();
        QCOMPARE(submittedBytes, qint64(CaptureFrames * VadFrameSamples * 2));
        QCOMPARE(c.lines().size(), 1);
        const auto line = c.lines().front().toMap();
        QCOMPARE(line.value(QStringLiteral("text")).toString(), QStringLiteral("mic mic mic mic mic mic tail"));
        QVERIFY(line.value(QStringLiteral("final")).toBool());
        QVERIFY(!line.value(QStringLiteral("incomplete")).toBool());
        c.close();
    }
    void continuousMicStaysOpenUntilFinalWhileSpkEndsIndependently() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());

        constexpr int SampleRate = 16000;
        constexpr int PacketFrames = 3040; // 190 ms, deliberately misaligned with one-second windows.
        int micFrames = 0, spkFrames = 0;
        quint64 micSequence = 0, spkSequence = 0;
        auto feed = [&](AudioSource source, qint16 sample, int count, int &frames, quint64 &sequence) {
            QByteArray pcm(count * 2, '\0');
            for (int i = 0; i < pcm.size(); i += 2)
                qToLittleEndian(sample, reinterpret_cast<uchar *>(pcm.data() + i));
            emit manager.pcmReady(id, 0, source, {pcm, sequence++, qint64(frames) * 1000000000LL / SampleRate});
            frames += count;
        };
        auto feedUntil = [&](int targetFrames) {
            while (micFrames < targetFrames) {
                feed(AudioSource::Mic, 1000, std::min(PacketFrames, targetFrames - micFrames), micFrames, micSequence);
                if (spkFrames < 6 * SampleRate) {
                    const bool quiet = spkFrames >= 3 * SampleRate && spkFrames < 4 * SampleRate;
                    const int end = (spkFrames < 3 * SampleRate ? 3 : quiet ? 4 : 6) * SampleRate;
                    feed(AudioSource::Spk, quiet ? 0 : -1000, std::min(PacketFrames, end - spkFrames),
                         spkFrames, spkSequence);
                }
                if (!workerBarrier(c)) return false;
                QCoreApplication::processEvents();
                if (!c.errorMessage().isEmpty()) return false;
            }
            return true;
        };
        auto linesFor = [&](QStringView source) {
            QList<QVariantMap> result;
            for (const auto &entry : c.lines()) {
                const auto line = entry.toMap();
                if (line.value(QStringLiteral("source")).toString() == source) result.append(line);
            }
            return result;
        };
        auto micWords = [](int windows) { return QStringLiteral("mic ").repeated(windows).trimmed(); };

        constexpr int VadFrameSamples = 512;
        constexpr int VadLookaheadSamples = SampleRate * 640 / 1000;
        constexpr int TwoSecondPreviewFrames =
            (2 * SampleRate + VadLookaheadSamples + VadFrameSamples - 1) / VadFrameSamples * VadFrameSamples;
        QVERIFY(feedUntil(TwoSecondPreviewFrames));
        auto mic = linesFor(u"mic");
        QCOMPARE(mic.size(), 1);
        QVERIFY(!mic[0].value(QStringLiteral("final")).toBool());
        QCOMPARE(mic[0].value(QStringLiteral("text")).toString(), micWords(2));
        const auto firstId = mic[0].value(QStringLiteral("id"));

        QVERIFY(feedUntil(22 * SampleRate + 6400));
        mic = linesFor(u"mic");
        QCOMPARE(mic.size(), 1);
        QCOMPARE(mic[0].value(QStringLiteral("id")), firstId);
        QCOMPARE(mic[0].value(QStringLiteral("text")).toString(), micWords(21));
        QVERIFY(!mic[0].value(QStringLiteral("final")).toBool());
        const auto spk = linesFor(u"spk");
        QCOMPARE(spk.size(), 2);
        QCOMPARE(spk[0].value(QStringLiteral("text")).toString(), QStringLiteral("spk spk spk"));
        QVERIFY(spk[0].value(QStringLiteral("final")).toBool());
        QVERIFY(spk[1].value(QStringLiteral("id")) != spk[0].value(QStringLiteral("id")));
        QCOMPARE(spk[1].value(QStringLiteral("text")).toString(), QStringLiteral("spk"));
        QVERIFY(!spk[1].value(QStringLiteral("final")).toBool());

        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        mic = linesFor(u"mic");
        QCOMPARE(mic.size(), 1);
        QCOMPARE(mic[0].value(QStringLiteral("id")), firstId);
        QCOMPARE(mic[0].value(QStringLiteral("text")).toString(), micWords(22) + QStringLiteral(" mic tail"));
        QVERIFY(mic[0].value(QStringLiteral("final")).toBool());
        QVERIFY(!mic[0].value(QStringLiteral("incomplete")).toBool());
        const auto spkFinal = linesFor(u"spk");
        QCOMPARE(spkFinal.size(), 2);
        QCOMPARE(spkFinal[0], spk[0]);
        QCOMPARE(spkFinal[1].value(QStringLiteral("id")), spk[1].value(QStringLiteral("id")));
        QCOMPARE(spkFinal[1].value(QStringLiteral("text")).toString(), QStringLiteral("spk spk spk tail"));
        QVERIFY(spkFinal[1].value(QStringLiteral("final")).toBool());
        QVERIFY(c.errorMessage().isEmpty());
        c.close();
    }
    void vadEndSeparatesNoisyMicWhileInferenceIsQueued() {
        qputenv("OFFLINE_FAKE_VAD_SCENARIO", "1");
        const auto restore = qScopeGuard([] { qunsetenv("OFFLINE_FAKE_VAD_SCENARIO"); });
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());

        QSemaphore entered, release;
        QMetaObject::invokeMethod(c.m_workerContext, [&] { entered.release(); release.acquire(); });
        if (!entered.tryAcquire(1, 3000)) {
            release.release(); workerBarrier(c); QFAIL("worker did not reach the queue barrier");
        }
        constexpr int FramesPerPacket = 512;
        const auto packet = [](qint16 value) {
            QByteArray pcm(FramesPerPacket * 2, Qt::Uninitialized);
            for (int offset = 0; offset < pcm.size(); offset += 2)
                qToLittleEndian(value, reinterpret_cast<uchar *>(pcm.data() + offset));
            return pcm;
        };
        const auto first = packet(1000);
        const auto noise = packet(600); // Above the old peak gate, below fake VAD speech.
        const auto second = packet(2000);
        const auto spk = packet(-1000);
        quint64 sequence = 0;
        const auto feed = [&](const QByteArray &pcm, int packets) {
            for (int n = 0; n < packets; ++n, ++sequence) {
                const auto timestamp = qint64(sequence) * 32'000'000;
                emit manager.pcmReady(id, 0, AudioSource::Mic, {pcm, sequence, timestamp});
                emit manager.pcmReady(id, 0, AudioSource::Spk, {spk, sequence, timestamp});
            }
        };
        feed(first, 34);
        feed(noise, 30);
        feed(second, 60);
        release.release();
        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();

        const auto linesFor = [&](QStringView source) {
            QList<QVariantMap> result;
            for (const auto &entry : c.lines()) {
                const auto line = entry.toMap();
                if (line.value(QStringLiteral("source")).toString() == source) result.append(line);
            }
            return result;
        };
        const auto mic = linesFor(u"mic");
        QCOMPARE(mic.size(), 2);
        const auto micA = mic[0].value(QStringLiteral("text")).toString();
        const auto micB = mic[1].value(QStringLiteral("text")).toString();
        QVERIFY(micA.contains(QStringLiteral("first")) && !micA.contains(QStringLiteral("second")));
        QVERIFY(micB.contains(QStringLiteral("second")) && !micB.contains(QStringLiteral("first")));
        QVERIFY(mic[0].value(QStringLiteral("final")).toBool());
        QVERIFY(!mic[1].value(QStringLiteral("final")).toBool());
        QVERIFY(mic[0].value(QStringLiteral("id")) != mic[1].value(QStringLiteral("id")));
        const auto spkLines = linesFor(u"spk");
        QCOMPARE(spkLines.size(), 1);
        QVERIFY(!spkLines[0].value(QStringLiteral("final")).toBool());

        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 5000);
        const auto finalMic = linesFor(u"mic");
        QCOMPARE(finalMic.size(), 2);
        QCOMPARE(finalMic[0], mic[0]);
        QCOMPARE(finalMic[1].value(QStringLiteral("id")), mic[1].value(QStringLiteral("id")));
        QVERIFY(finalMic[1].value(QStringLiteral("final")).toBool());
        QVERIFY(finalMic[1].value(QStringLiteral("text")).toString().contains(QStringLiteral("tail")));
        const auto finalSpk = linesFor(u"spk");
        QCOMPARE(finalSpk.size(), 1);
        QCOMPARE(finalSpk[0].value(QStringLiteral("id")), spkLines[0].value(QStringLiteral("id")));
        QVERIFY(finalSpk[0].value(QStringLiteral("final")).toBool());
        QVERIFY(c.errorMessage().isEmpty());
        c.close();
    }
    void vadEndOnlySubmitsSpeechAndTwoHundredMillisecondsOfTail() {
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        QVERIFY2(load(c), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c, false);
        QVERIFY(!id.isEmpty());
        resetCounters();

        constexpr int VadFrameSamples = 512;
        constexpr int SpeechFrames = 16;
        constexpr int QuietFrames = 30;
        constexpr int ExpectedTailSamples = 3200;
        const auto packet = [](qint16 sample) {
            QByteArray pcm(VadFrameSamples * 2, Qt::Uninitialized);
            for (int offset = 0; offset < pcm.size(); offset += 2)
                qToLittleEndian(sample, reinterpret_cast<uchar *>(pcm.data() + offset));
            return pcm;
        };
        const auto speech = packet(1000);
        const auto background = packet(600);
        for (int n = 0; n < SpeechFrames + QuietFrames; ++n) {
            emit manager.pcmReady(id, 0, AudioSource::Mic,
                                  {n < SpeechFrames ? speech : background, quint64(n), qint64(n) * 32'000'000});
        }

        QVERIFY(workerBarrier(c));
        QCoreApplication::processEvents();
        QCOMPARE(c.lines().size(), 1);
        QVERIFY(c.lines().front().toMap().value(QStringLiteral("final")).toBool());
        QCOMPARE(frameCount(0), uint64_t(SpeechFrames * VadFrameSamples + ExpectedTailSamples));
        QCOMPARE(pushCount(0), 1);
        c.close();
    }
    void realGpuDualChannelSpeechThroughCaptureSignals() {
        const QString model = qEnvironmentVariable("CHUNFE_REAL_ASR_MODEL");
        const QString path = qEnvironmentVariable("CHUNFE_REAL_ASR_WAV");
        if (model.isEmpty() || path.isEmpty())
            QSKIP("Set CHUNFE_REAL_ASR_MODEL and CHUNFE_REAL_ASR_WAV for the optional real GPU test with the bundled runtime");

        QByteArray pcm = readPcmFixture(path);
        QVERIFY2(pcm.size() >= 115200, "fixture must be 16 kHz mono PCM16 and at least 3.6 seconds long");
        pcm.truncate(115200); // Non-integral window exercises finish() tail handling.

        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        qInfo().noquote() << "Bundled runtime:" << c.m_runtimePath;
        c.loadEngine(model);
        QTRY_VERIFY_WITH_TIMEOUT(c.engineStatus() != QStringLiteral("starting"), 120000);
        QVERIFY2(c.engineStatus() == QStringLiteral("ready"), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c);
        QVERIFY(!id.isEmpty());
        for (int offset = 0; offset < pcm.size(); offset += 6400) {
            const auto audio = pcm.mid(offset, 6400);
            const CapturedAudio packet{audio, quint64(offset / 6400), qint64(offset / 2) * 1000000000LL / 16000};
            emit manager.pcmReady(id, 0, AudioSource::Mic, packet);
            emit manager.pcmReady(id, 0, AudioSource::Spk, packet);
            QTest::qWait(int(audio.size() / 32)); // Feed at real-time rate, allowing inference to keep up.
            QVERIFY2(!c.m_sourceFailed[0] && !c.m_sourceFailed[1], qPrintable(c.errorMessage()));
        }
        QTRY_VERIFY_WITH_TIMEOUT(c.m_pendingBytes[0] == 0 && c.m_pendingBytes[1] == 0, 120000);
        auto textForSource = [&c](const QString &source) {
            QString text;
            for (const auto &entry : c.lines()) {
                const auto line = entry.toMap();
                if (line.value(QStringLiteral("source")).toString() == source)
                    text += line.value(QStringLiteral("text")).toString();
            }
            return text;
        };
        const QString micBeforeFinish = textForSource(QStringLiteral("mic"));
        const QString spkBeforeFinish = textForSource(QStringLiteral("spk"));
        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 120000);
        QString mic, spk;
        int micCapsules = 0, spkCapsules = 0;
        for (const auto &entry : c.lines()) {
            const auto line = entry.toMap();
            if (!line.value(QStringLiteral("final")).toBool()) continue;
            const bool isMic = line.value(QStringLiteral("source")).toString() == QStringLiteral("mic");
            auto &text = isMic ? mic : spk;
            ++(isMic ? micCapsules : spkCapsules);
            text += line.value(QStringLiteral("text")).toString();
        }
        qInfo().noquote() << "MIC before finish:" << micBeforeFinish << "after:" << mic;
        qInfo().noquote() << "SPK before finish:" << spkBeforeFinish << "after:" << spk;
        qInfo() << "Final capsules (MIC, SPK):" << micCapsules << spkCapsules;
        QVERIFY2(!mic.isEmpty() && !spk.isEmpty(), "expected finalized recognition text from both channels");
        if (qEnvironmentVariableIsSet("CHUNFE_REAL_EXPECT_SINGLE_CAPSULE")) {
            QCOMPARE(micCapsules, 1);
            QCOMPARE(spkCapsules, 1);
        }
        c.close();
    }
    void realGpuContinuousMicPreservesTextThroughFinal() {
        const QString model = qEnvironmentVariable("CHUNFE_REAL_ASR_MODEL");
        const QString path = qEnvironmentVariable("CHUNFE_REAL_ASR_WAV");
        if (model.isEmpty() || path.isEmpty())
            QSKIP("Set CHUNFE_REAL_ASR_MODEL and CHUNFE_REAL_ASR_WAV for the optional continuous MIC GPU test");

        constexpr int PcmBytesPerMs = 32;
        constexpr int TileMs = 3600;
        constexpr int DurationMs = 22400;
        constexpr int PacketMs = 200;
        const QByteArray fixture = readPcmFixture(path);
        QVERIFY2(fixture.size() >= TileMs * PcmBytesPerMs, "fixture must be 16 kHz mono PCM16 and at least 3.6 seconds long");
        const QByteArray speech = fixture.first(TileMs * PcmBytesPerMs)
            .repeated((DurationMs + TileMs - 1) / TileMs).first(DurationMs * PcmBytesPerMs);

        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager, m_configFilePath);
        c.loadEngine(model);
        QTRY_VERIFY_WITH_TIMEOUT(c.engineStatus() != QStringLiteral("starting"), 120000);
        QVERIFY2(c.engineStatus() == QStringLiteral("ready"), qPrintable(c.engineError()));
        const auto id = startCaptureSignals(c, false);
        QVERIFY(!id.isEmpty());

        QElapsedTimer playback; playback.start();
        for (int ms = 0; ms < DurationMs; ms += PacketMs) {
            const CapturedAudio packet{speech.mid(ms * PcmBytesPerMs, PacketMs * PcmBytesPerMs),
                                       quint64(ms / PacketMs), qint64(ms) * 1000000};
            emit manager.pcmReady(id, 0, AudioSource::Mic, packet);
            QTest::qWait(int(std::max<qint64>(0, ms + PacketMs - playback.elapsed())));
            QVERIFY2(!c.m_sourceFailed[0], qPrintable(c.errorMessage()));
        }
        QTRY_COMPARE_WITH_TIMEOUT(c.m_pendingBytes[0], 0, 10000);
        QCOMPARE(c.status(), QStringLiteral("running"));
        auto lettersAndNumbers = [](const QVariantList &lines) {
            QString result;
            for (const auto &entry : lines) {
                const auto text = entry.toMap().value(QStringLiteral("text")).toString();
                for (const QChar ch : text)
                    if (ch.isLetterOrNumber()) result += ch;
            }
            return result;
        };
        QVERIFY(!lettersAndNumbers(c.lines()).isEmpty());
        QVariantList finalBeforeStop;
        for (const auto &entry : c.lines())
            if (entry.toMap().value(QStringLiteral("final")).toBool()) finalBeforeStop.append(entry);

        drainInjectedCapture(c, manager, id);
        QTRY_COMPARE_WITH_TIMEOUT(c.status(), QStringLiteral("stopped"), 10000);
        const auto finalLines = c.lines();
        QCOMPARE(finalLines.mid(0, finalBeforeStop.size()), finalBeforeStop);
        QVERIFY(!lettersAndNumbers(finalLines).isEmpty());
        for (const auto &entry : finalLines) {
            const auto line = entry.toMap();
            QCOMPARE(line.value(QStringLiteral("source")).toString(), QStringLiteral("mic"));
            QVERIFY(line.value(QStringLiteral("final")).toBool());
            QVERIFY(!line.value(QStringLiteral("incomplete")).toBool());
            QVERIFY(!line.value(QStringLiteral("text")).toString().isEmpty());
        }
        QVERIFY(c.errorMessage().isEmpty());
        qInfo() << "Continuous MIC replay ms:" << DurationMs << "capsules finalized before stop:" << finalBeforeStop.size()
                << "total:" << finalLines.size();
        c.close();
    }
};
QTEST_GUILESS_MAIN(OfflineTranscriptionLifecycleTests)
#include "offline_transcription_lifecycle_tests.moc"
