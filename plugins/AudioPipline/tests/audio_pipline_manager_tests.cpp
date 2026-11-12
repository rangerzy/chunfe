#include "audio_pipline_manager.h"
#include "audio_level_test.h"
#include "captures/audio_capture_context.h"
#include "captures/audio_capture_worker.h"
#if defined(Q_OS_MACOS)
#include "fake_mac_system_capture.h"
#endif

#include <miniaudio.h>
#include <QRegularExpression>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <utility>

namespace {
struct FakeAudioContext
{
    ma_result initResult = MA_SUCCESS;
    ma_result devicesResult = MA_SUCCESS;
    int initCalls = 0;
    int devicesCalls = 0;
    int uninitCalls = 0;
    bool provideInput = false;
    bool provideOutput = false;
    ma_device_info input{};
    ma_device_info output{};
    ma_device_data_proc callback = nullptr;
    ma_device_notification_proc notification = nullptr;
    ma_device* activeDevice = nullptr;
    ma_format requestedFormat = ma_format_unknown;
    ma_uint32 requestedRate = 0;
    ma_uint32 requestedChannels = 0;
    int deviceStartCalls = 0;
    int deviceStopCalls = 0;
    int failStartCount = 0;
    int failStopCount = 0;
    bool emitFrameOnStop = false;
    bool emitFrameOnStart = false;
};

FakeAudioContext fake;

// These flags are shared with the fake backend thread; do not use the
// ordinary test counters to observe an operation that is still in progress.
std::atomic<int> initDelayMs{0};
std::atomic<int> enumerateDelayMs{0};
std::atomic<int> startDelayMs{0};
std::atomic<int> stopDelayMs{0};
std::atomic<bool> initInProgress{false};
std::atomic<bool> enumerateInProgress{false};
std::atomic<bool> startInProgress{false};
std::atomic<bool> stopInProgress{false};
std::atomic<bool> deviceIsLive{false};

void delay(std::atomic<int>& milliseconds, std::atomic<bool>& inProgress)
{
    inProgress.store(true, std::memory_order_release);
    const auto value = milliseconds.load(std::memory_order_relaxed);
    if (value > 0) std::this_thread::sleep_for(std::chrono::milliseconds(value));
    inProgress.store(false, std::memory_order_release);
}

// A real enabled-stage consumer: every audio/cancellation/boundary block is
// acknowledged through the queue API. The manager must never do this for us.
class ConvertConsumer final : public QObject
{
public:
    ConvertConsumer(AudioPiplineManager& manager, AudioPipline::CaptureId id)
        : m_manager(manager), m_id(std::move(id))
    {
        connect(&manager, &AudioPiplineManager::queueUpdated, this,
                [this](const AudioPipline::CaptureId& id, quint64) {
            if (id == m_id) consume();
        }, Qt::QueuedConnection);
        QTimer::singleShot(0, this, [this] { consume(); });
    }

    QString error;
    int audioBlocks = 0;
    int segmentEnds = 0;
    int streamEnds = 0;
    int cancelledBlocks = 0;
    QList<AudioPipline::SharedBlockHeader> acknowledgedBoundaries;

private:
    void consume()
    {
        using namespace AudioPipline;
        if (!error.isEmpty()) return;
        for (int processed = 0; processed < 512; ++processed) {
            auto block = m_manager.pcmQueue().tryAcquire(m_id, Stage::Convert);
            if (block.status == AcquireStatus::WouldBlock || block.status == AcquireStatus::Closed) return;
            if (block.status != AcquireStatus::Acquired || !block.block) {
                error = QStringLiteral("Convert acquisition failed: ") + block.error.message;
                return;
            }
            const auto header = block.block->header();
            const PcmInfo info{header.format, header.frameCount, header.validBytes, header.pcmTimestampNs};
            const auto result = m_manager.pcmQueue().commit(*block.block, info);
            if (result.status != QueueStatus::Ok && result.status != QueueStatus::Cancelled) {
                error = QStringLiteral("Convert acknowledgement failed: ") + result.error.message;
                return;
            }
            segmentEnds += header.kind == std::uint32_t(BlockKind::SegmentEnd);
            streamEnds += header.kind == std::uint32_t(BlockKind::StreamEnd);
            audioBlocks += header.kind == std::uint32_t(BlockKind::Audio);
            cancelledBlocks += result.status == QueueStatus::Cancelled;
            if (header.kind != std::uint32_t(BlockKind::Audio)) acknowledgedBoundaries.append(header);
        }
        error = QStringLiteral("Convert consumer did not reach the end of the available blocks");
    }

    AudioPiplineManager& m_manager;
    AudioPipline::CaptureId m_id;
};
}

// Fake only the miniaudio boundary; the manager under test is production code.
ma_result ma_context_init(const ma_backend[], ma_uint32, const ma_context_config*, ma_context* context)
{
    ++fake.initCalls;
    context->backend = ma_backend_coreaudio;
    return fake.initResult;
}

ma_result ma_context_uninit(ma_context*)
{
    ++fake.uninitCalls;
    return MA_SUCCESS;
}

ma_result ma_context_get_devices(ma_context*, ma_device_info** playback, ma_uint32* playbackCount,
                                 ma_device_info** capture, ma_uint32* captureCount)
{
    delay(enumerateDelayMs, enumerateInProgress);
    ++fake.devicesCalls;
    if (fake.devicesResult == MA_SUCCESS) {
        *playback = fake.provideOutput ? &fake.output : nullptr;
        *playbackCount = fake.provideOutput ? 1 : 0;
        *capture = fake.provideInput ? &fake.input : nullptr;
        *captureCount = fake.provideInput ? 1 : 0;
    }
    return fake.devicesResult;
}

ma_device_config ma_device_config_init(ma_device_type type)
{
    ma_device_config config{};
    config.deviceType = type;
    return config;
}

ma_result ma_device_init(ma_context*, const ma_device_config* config, ma_device* device)
{
    delay(initDelayMs, initInProgress);
    device->pUserData = config->pUserData;
    fake.requestedFormat = config->capture.format;
    fake.requestedRate = config->sampleRate;
    fake.requestedChannels = config->capture.channels;
    device->sampleRate = config->sampleRate;
    device->capture.channels = config->capture.channels;
    fake.callback = config->dataCallback;
    fake.notification = config->notificationCallback;
    fake.activeDevice = device;
    deviceIsLive.store(true, std::memory_order_release);
    return MA_SUCCESS;
}

ma_result ma_device_start(ma_device*)
{
    delay(startDelayMs, startInProgress);
    ++fake.deviceStartCalls;
    if (fake.emitFrameOnStart) {
        const std::int16_t sample = 16384;
        fake.callback(fake.activeDevice, nullptr, &sample, 1);
    }
    if (fake.failStartCount > 0) { --fake.failStartCount; return MA_ERROR; }
    return MA_SUCCESS;
}
ma_result ma_device_stop(ma_device*)
{
    delay(stopDelayMs, stopInProgress);
    ++fake.deviceStopCalls;
    if (fake.emitFrameOnStop && fake.callback && fake.activeDevice) {
        const std::int16_t sample = 24576;
        fake.callback(fake.activeDevice, nullptr, &sample, 1);
    }
    if (fake.failStopCount > 0) { --fake.failStopCount; return MA_ERROR; }
    return MA_SUCCESS;
}
void ma_device_uninit(ma_device*)
{
    fake.activeDevice = nullptr;
    deviceIsLive.store(false, std::memory_order_release);
}

class AudioPipelineManagerTests : public QObject
{
    Q_OBJECT

private:
    void prepareInput()
    {
        fake.provideInput = true;
        std::strcpy(fake.input.name, "Fake microphone");
        std::strcpy(fake.input.id.coreaudio, "fake-microphone");
        fake.input.isDefault = true;
    }

    void prepareOutput()
    {
        fake.provideOutput = true;
        std::strcpy(fake.output.name, "Fake speaker");
        std::strcpy(fake.output.id.coreaudio, "fake-speaker");
        fake.output.isDefault = true;
    }

private slots:
    void init()
    {
        fake = {};
        initDelayMs = 0; enumerateDelayMs = 0; startDelayMs = 0; stopDelayMs = 0;
        initInProgress = false; enumerateInProgress = false; startInProgress = false; stopInProgress = false;
        deviceIsLive = false;
#if defined(Q_OS_MACOS)
        FakeMacSystemCapture::reset();
#endif
    }

    void failedInitializationDoesNotEnumerateOrUninitialize()
    {
        fake.initResult = MA_ERROR;
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral(".*初始化音频上下文失败.*")));
        {
            AudioPiplineManager manager;
            QCOMPARE(fake.initCalls, 1);
            QCOMPARE(fake.devicesCalls, 0);
            manager.getDevices();
            QCOMPARE(fake.devicesCalls, 0);
        }
        QCOMPARE(fake.uninitCalls, 0);
    }

    void failedEnumerationUninitializesExactlyOnce()
    {
        fake.devicesResult = MA_ERROR;
        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral(".*获取设备列表失败.*")));
        {
            AudioPiplineManager manager;
            QCOMPARE(fake.initCalls, 1);
            QCOMPARE(fake.devicesCalls, 1);
            QCOMPARE(fake.uninitCalls, 0);
        }
        QCOMPARE(fake.uninitCalls, 1);
    }

    void successfulEnumerationUninitializesExactlyOnce()
    {
        {
            AudioPiplineManager manager;
            QCOMPARE(fake.initCalls, 1);
            QCOMPARE(fake.devicesCalls, 1);
            QCOMPARE(fake.uninitCalls, 0);
        }
        QCOMPARE(fake.uninitCalls, 1);
    }

#if defined(Q_OS_MACOS)
    void enumerationReportsCaptureCandidates_data()
    {
        QTest::addColumn<bool>("inputCandidate");
        QTest::addColumn<bool>("outputCandidate");
        QTest::addColumn<bool>("outputSupported");
        QTest::newRow("both-valid") << true << true << true;
        QTest::newRow("input-disconnected") << false << true << true;
        QTest::newRow("output-unusable") << true << false << true;
        QTest::newRow("output-unsupported") << true << true << false;
    }

    void enumerationReportsCaptureCandidates()
    {
        using namespace AudioPipline;
        QFETCH(bool, inputCandidate);
        QFETCH(bool, outputCandidate);
        QFETCH(bool, outputSupported);
        prepareInput();
        prepareOutput();
        auto& topology = FakeMacSystemCapture::state();
        topology.inputCandidate = inputCandidate;
        topology.outputCandidate = outputCandidate;
        topology.supported = outputSupported;

        AudioCaptureContext context;
        const auto environment = context.snapshot();
        QCOMPARE(environment.devices.size(), 2);
        QCOMPARE(environment.devices[0].source, AudioSource::Mic);
        QCOMPARE(environment.devices[0].available, inputCandidate);
        QCOMPARE(environment.devices[1].source, AudioSource::Spk);
        QCOMPARE(environment.devices[1].available, outputCandidate && outputSupported);
        QCOMPARE(fake.deviceStartCalls, 0);
        QCOMPARE(topology.startCalls, 0);
    }
#endif

    void workerSplitsPcmAndGatesCallbacksDuringStop()
    {
        using namespace AudioPipline;
        prepareInput();
        auto context = std::make_shared<AudioCaptureContext>();
        context->refresh();
        auto queue = std::make_shared<SharedPcmQueue>();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const ProcessingStages stages = ProcessingStage::Convert | ProcessingStage::Resample |
                                        ProcessingStage::Save | ProcessingStage::Asr;
        QCOMPARE(queue->create({4, 8, stages}, id).status, QueueStatus::Ok);
        AudioCaptureWorker worker(AudioSource::Mic, context);
        AudioCaptureWorker::Output output{queue, id, std::chrono::steady_clock::now(), 8, 1};
        QCOMPARE(worker.prepare({}, output, false).code, AudioErrorCode::None);
        QCOMPARE(worker.startCapture().code, AudioErrorCode::None);

        const std::int16_t samples[] = {16384, -16384, 32767, -32768, 0};
        fake.callback(fake.activeDevice, nullptr, samples, 5);
        QCOMPARE(queue->snapshot().usedSlots, 0u);
        worker.setAccepting(true);
        fake.callback(fake.activeDevice, nullptr, samples, 5);
        QCOMPARE(queue->snapshot().usedSlots, 2u);
        QCOMPARE(fake.requestedFormat, ma_format_s16);
        QCOMPARE(fake.requestedRate, 16000u);
        QCOMPARE(fake.requestedChannels, 1u);
        const auto level = worker.level();
        QCOMPARE(level.peak, 1.0f);
        QVERIFY(std::abs(level.rms - 0.70710678f) < 0.00002f);
        for (int index = 0; index < 2; ++index) {
            auto block = queue->tryAcquire(id, Stage::Convert);
            QCOMPARE(block.status, AcquireStatus::Acquired);
            QCOMPARE(block.block->header().source, std::uint32_t(AudioSource::Mic));
            const std::uint32_t frames = index == 1 ? 1 : 4;
            QCOMPARE(block.block->header().frameCount, frames);
            QCOMPARE(block.block->header().validBytes, frames * sizeof(std::int16_t));
            QCOMPARE(block.block->header().format.sampleRate, 16000u);
            QCOMPARE(block.block->header().format.channels, 1u);
            QCOMPARE(block.block->header().format.sampleType, SampleType::Int16);
            QCOMPARE(std::memcmp(block.block->data(), samples + index * 4, frames * sizeof(std::int16_t)), 0);
            PcmInfo info{block.block->header().format, frames, frames * std::uint32_t(sizeof(std::int16_t)),
                         block.block->header().pcmTimestampNs};
            QCOMPARE(queue->commit(*block.block, info).status, QueueStatus::Ok);
        }
        fake.emitFrameOnStop = true;
        QCOMPARE(worker.stopCapture().code, AudioErrorCode::None);
        QCOMPARE(queue->snapshot().usedSlots, 2u);
        QVERIFY(!deviceIsLive.load());
    }

    void workerResumeDoesNotSwitchToNewDefaultDevice()
    {
        using namespace AudioPipline;
        prepareInput();
        auto context = std::make_shared<AudioCaptureContext>();
        context->refresh();
        auto queue = std::make_shared<SharedPcmQueue>();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCOMPARE(queue->create({4, 64}, id).status, QueueStatus::Ok);
        AudioCaptureWorker worker(AudioSource::Mic, context);
        AudioCaptureWorker::Output output{queue, id, std::chrono::steady_clock::now(), 64, 1};
        QCOMPARE(worker.prepare({}, output, false).code, AudioErrorCode::None);
        const auto originalDevice = worker.info().device.id;
        QVERIFY(!originalDevice.isEmpty());
        QCOMPARE(worker.startCapture().code, AudioErrorCode::None);
        QCOMPARE(worker.stopCapture().code, AudioErrorCode::None);
        std::strcpy(fake.input.id.coreaudio, "replacement-microphone");
        std::strcpy(fake.input.name, "Replacement microphone");
        context->refresh();
        QCOMPARE(worker.prepare({}, output, true).code, AudioErrorCode::DeviceUnavailable);
        QVERIFY(!deviceIsLive.load());
        QCOMPARE(worker.info().device.id, originalDevice);
    }

#if defined(Q_OS_MACOS)
    void systemCaptureSplitBlocksKeepTheirTimeOffsets()
    {
        using namespace AudioPipline;
        prepareOutput();
        auto context = std::make_shared<AudioCaptureContext>();
        context->refresh();
        auto queue = std::make_shared<SharedPcmQueue>();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QCOMPARE(queue->create({4, 2048, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
        AudioCaptureWorker worker(AudioSource::Spk, context);
        AudioCaptureWorker::Output output{queue, id, std::chrono::steady_clock::now() - std::chrono::seconds(1), 2048, 1};
        QCOMPARE(worker.prepare({}, output, false).code, AudioErrorCode::None);
        QCOMPARE(worker.startCapture().code, AudioErrorCode::None);
        worker.setAccepting(true);

        // 模拟一个原始输入被拆成两个 64 ms 输出块：第一块后面仍有 64 ms 待交付。
        const std::int16_t samples[1024] = {};
        FakeMacSystemCapture::sendSamples(samples, 1024, 64000000);
        FakeMacSystemCapture::sendSamples(samples, 1024);
        auto first = queue->tryAcquire(id, Stage::Convert);
        QCOMPARE(first.status, AcquireStatus::Acquired);
        const auto firstHeader = first.block->header();
        QCOMPARE(firstHeader.source, std::uint32_t(AudioSource::Spk));
        const PcmInfo firstInfo{firstHeader.format, firstHeader.frameCount, firstHeader.validBytes, firstHeader.pcmTimestampNs};
        QCOMPARE(queue->commit(*first.block, firstInfo).status, QueueStatus::Ok);
        auto second = queue->tryAcquire(id, Stage::Convert);
        QCOMPARE(second.status, AcquireStatus::Acquired);
        const auto delta = second.block->header().pcmTimestampNs - firstHeader.pcmTimestampNs;
        QVERIFY(delta >= 64000000 && delta < 264000000);
        QCOMPARE(second.block->header().source, std::uint32_t(AudioSource::Spk));
        QCOMPARE(worker.stopCapture().code, AudioErrorCode::None);
    }

    void audioLevelBridgeReceivesBothRealWorkerLevels()
    {
        using namespace AudioPipline;
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        AudioLevelTest meter(manager);
        QString microphoneId;
        QString systemId;
        for (const auto& device : manager.environment().devices) {
            if (device.source == AudioSource::Mic) microphoneId = device.id;
            if (device.source == AudioSource::Spk) systemId = device.id;
        }
        QCOMPARE(microphoneId, QStringLiteral("fake-microphone"));
        QCOMPARE(systemId, QStringLiteral("fake-speaker"));

        meter.start(microphoneId, systemId);
        QVERIFY(meter.active());
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QCOMPARE(manager.capture().runningSources, AudioSource::Mic | AudioSource::Spk);
        QVERIFY(fake.callback && fake.activeDevice);
        QVERIFY(FakeMacSystemCapture::state().callback);

        // Running is published before the queued activation commands complete.
        // Repeated bounded injection exercises the actual callbacks without
        // assuming that the first frame after Running was accepted.
        const std::int16_t microphone[] = {8192, -8192};
        const std::int16_t system[] = {24576, -24576};
        QTimer inject;
        connect(&inject, &QTimer::timeout, this, [&] {
            fake.callback(fake.activeDevice, nullptr, microphone, 2);
            FakeMacSystemCapture::sendSamples(system, 2);
        });
        inject.start(10);
        QTRY_VERIFY_WITH_TIMEOUT(meter.microphoneLevel() > 0.2 && meter.systemLevel() > 0.7, 3000);
        QCOMPARE(meter.microphoneLevel(), qreal(0.25));
        QCOMPARE(meter.systemLevel(), qreal(0.75));
        inject.stop();

        meter.stop();
        QVERIFY(!meter.active());
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(!meter.stopping());
        QCOMPARE(meter.microphoneLevel(), qreal(0));
        QCOMPARE(meter.systemLevel(), qreal(0));
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QVERIFY(!FakeMacSystemCapture::state().live);
    }

    void bothSourcesWaitForStartupBeforePublishingPcm()
    {
        using namespace AudioPipline;
        prepareInput();
        prepareOutput();
        fake.emitFrameOnStart = true;
        auto& system = FakeMacSystemCapture::state();
        system.emitFrameOnStart = true;
        system.startDelayMs = 300;
        AudioPiplineManager manager;
        QSignalSpy startedSignal(&manager, &AudioPiplineManager::captureStarted);
        bool observedSystemStartup = false;
        bool publishedBeforeReady = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            if (!system.startInProgress.load(std::memory_order_acquire)) return;
            observedSystemStartup = true;
            publishedBeforeReady |= manager.capture().state != CaptureState::Starting ||
                                    !startedSignal.isEmpty() || manager.queueSnapshot().usedSlots != 0;
        });
        heartbeat.start(5);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.queue.enabledStages = ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == CaptureState::Running ||
                                 manager.capture().state == CaptureState::Idle, 3000);
        QCOMPARE(manager.capture().state, CaptureState::Running);
        QVERIFY(observedSystemStartup);
        QVERIFY(!publishedBeforeReady);
        QCOMPARE(startedSignal.count(), 1);
        const auto captureInfo = qvariant_cast<CaptureInfo>(startedSignal.at(0).at(1));
        QCOMPARE(captureInfo.sources.size(), 2);
        for (const auto& source : captureInfo.sources) {
            QCOMPARE(source.format.sampleRate, 16000u);
            QCOMPARE(source.format.channels, 1u);
            QCOMPARE(source.format.sampleType, SampleType::Int16);
        }
        QCOMPARE(manager.capture().runningSources, AudioSource::Mic | AudioSource::Spk);
        QCOMPARE(manager.queueSnapshot().usedSlots, 0u);

        const std::int16_t mic[] = {16384, -16384};
        const std::int16_t spk[] = {8192, -8192};
        fake.callback(fake.activeDevice, nullptr, mic, 2);
        FakeMacSystemCapture::sendSamples(spk, 2);
        for (AudioSource source : {AudioSource::Mic, AudioSource::Spk}) {
            auto block = manager.pcmQueue().tryAcquire(started.captureId, Stage::Convert);
            QCOMPARE(block.status, AcquireStatus::Acquired);
            QCOMPARE(block.block->header().source, std::uint32_t(source));
            const std::uint32_t bytes = source == AudioSource::Mic ? sizeof(mic) : sizeof(spk);
            QCOMPARE(block.block->header().validBytes, bytes);
            QCOMPARE(block.block->header().format.sampleRate, 16000u);
            QCOMPARE(block.block->header().format.channels, 1u);
            QCOMPARE(block.block->header().format.sampleType, SampleType::Int16);
            QCOMPARE(block.block->header().format.layout, PcmLayout::Interleaved);
            QCOMPARE(std::memcmp(block.block->data(), source == AudioSource::Mic ? mic : spk, bytes), 0);
            PcmInfo info{block.block->header().format, 2, bytes, block.block->header().pcmTimestampNs};
            QCOMPARE(manager.pcmQueue().commit(*block.block, info).status, QueueStatus::Ok);
        }
        ConvertConsumer consumer(manager, started.captureId);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
        QCOMPARE(consumer.streamEnds, 2);
        QVERIFY(!deviceIsLive.load());
        QVERIFY(!system.live);
    }

    void failureDuringOtherSourceStartupNeverPublishesCaptureStarted()
    {
        using namespace AudioPipline;
        prepareInput();
        prepareOutput();
        fake.emitFrameOnStart = true;
        auto& system = FakeMacSystemCapture::state();
        system.startDelayMs = 300;
        system.emitFrameOnStart = true;
        AudioPiplineManager manager;
        QSignalSpy startedSignals(&manager, &AudioPiplineManager::captureStarted);
        bool disconnectedDuringSystemStartup = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            if (disconnectedDuringSystemStartup ||
                !system.startInProgress.load(std::memory_order_acquire)) return;
            disconnectedDuringSystemStartup = true;
            ma_device_notification disconnected{};
            disconnected.pDevice = fake.activeDevice;
            disconnected.type = ma_device_notification_type_stopped;
            fake.notification(&disconnected);
        });
        heartbeat.start(5);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.queue.enabledStages = ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        ConvertConsumer consumer(manager, started.captureId);
        QTRY_VERIFY_WITH_TIMEOUT(disconnectedDuringSystemStartup, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(startedSignals.isEmpty());
        QCOMPARE(consumer.audioBlocks, 0);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QVERIFY(!system.live);
    }

    void sourceFailurePausePreservesBothBoundaryKinds_data()
    {
        QTest::addColumn<bool>("pauseDuringStopRetry");
        QTest::newRow("pause-from-disconnection-issue") << false;
        QTest::newRow("pause-during-failed-source-stop-retry") << true;
    }

    void sourceFailurePausePreservesBothBoundaryKinds()
    {
        using namespace AudioPipline;
        QFETCH(bool, pauseDuringStopRetry);
        const auto failedSource = pauseDuringStopRetry ? AudioSource::Spk : AudioSource::Mic;
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.queue.enabledStages = ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);

        CommandResult pauseResult;
        bool sawDisconnection = false;
        bool sawStopFailure = false;
        bool pauseRequested = false;
        connect(&manager, &AudioPiplineManager::issueReported, &manager,
                [&](const AudioIssue& issue) {
            if (issue.captureId != started.captureId || issue.error.source != failedSource) return;
            sawDisconnection |= issue.error.code == AudioErrorCode::DeviceDisconnected;
            sawStopFailure |= issue.error.code == AudioErrorCode::BackendStopFailed;
            const auto trigger = pauseDuringStopRetry ? AudioErrorCode::BackendStopFailed
                                                     : AudioErrorCode::DeviceDisconnected;
            if (!pauseRequested && issue.error.code == trigger) {
                pauseRequested = true;
                pauseResult = manager.pauseCapture(started.captureId);
            }
        }, Qt::DirectConnection);
        if (pauseDuringStopRetry) {
            FakeMacSystemCapture::state().failStopCount = 1;
            FakeMacSystemCapture::sendFailure();
        } else {
            ma_device_notification disconnected{};
            disconnected.pDevice = fake.activeDevice;
            disconnected.type = ma_device_notification_type_stopped;
            fake.notification(&disconnected);
        }
        QTRY_VERIFY_WITH_TIMEOUT(pauseRequested, 3000);
        QVERIFY(pauseResult.accepted);
        QVERIFY(sawDisconnection);
        if (pauseDuringStopRetry) QVERIFY(sawStopFailure);

        QTRY_COMPARE_WITH_TIMEOUT(manager.queueSnapshot().usedSlots, 2u, 3000);
        AudioSources acknowledged;
        for (int index = 0; index < 2; ++index) {
            auto boundary = manager.pcmQueue().tryAcquire(started.captureId, Stage::Convert);
            QCOMPARE(boundary.status, AcquireStatus::Acquired);
            const auto header = boundary.block->header();
            const auto source = static_cast<AudioSource>(header.source);
            QVERIFY(source == AudioSource::Mic || source == AudioSource::Spk);
            QVERIFY(!acknowledged.testFlag(source));
            if (source == failedSource) {
                QCOMPARE(header.kind, std::uint32_t(BlockKind::StreamEnd));
                QCOMPARE(header.errorCode, std::uint32_t(AudioErrorCode::DeviceDisconnected));
            } else {
                QCOMPARE(header.kind, std::uint32_t(BlockKind::SegmentEnd));
                QCOMPARE(header.errorCode, std::uint32_t(AudioErrorCode::None));
                QVERIFY(header.flags & std::uint32_t(BlockFlag::Cancelled));
            }
            QTest::qWait(30);
            QCOMPARE(manager.capture().state, CaptureState::Stopping);
            const PcmInfo info{header.format, header.frameCount, header.validBytes, header.pcmTimestampNs};
            const auto result = manager.pcmQueue().commit(*boundary.block, info);
            QVERIFY(result.status == QueueStatus::Ok || result.status == QueueStatus::Cancelled);
            acknowledged |= source;
        }
        QCOMPARE(acknowledged, AudioSource::Mic | AudioSource::Spk);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);
        ConvertConsumer consumer(manager, started.captureId);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
    }

    void failedResumeSourceIsFinalizedAcrossIssueReentry_data()
    {
        QTest::addColumn<bool>("stopFromIssue");
        QTest::newRow("nested-event-loop-until-survivor-running") << false;
        QTest::newRow("stop-directly-from-resume-failure-issue") << true;
    }

    void failedResumeSourceIsFinalizedAcrossIssueReentry()
    {
        using namespace AudioPipline;
        QFETCH(bool, stopFromIssue);
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.queue.enabledStages = ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        ConvertConsumer consumer(manager, started.captureId);
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);
        QCOMPARE(consumer.segmentEnds, 2);

        bool enteredIssue = false;
        bool survivorReachedRunningInsideIssue = false;
        CommandResult stopResult;
        connect(&manager, &AudioPiplineManager::issueReported, &manager,
                [&](const AudioIssue& issue) {
            if (enteredIssue || issue.captureId != started.captureId ||
                issue.error.source != AudioSource::Mic ||
                issue.error.code != AudioErrorCode::BackendStartFailed) return;
            enteredIssue = true;
            if (stopFromIssue) {
                stopResult = manager.stopCapture(started.captureId, StopMode::Discard);
                return;
            }
            QEventLoop nested;
            connect(&manager, &AudioPiplineManager::captureChanged, &nested,
                    [&](const CaptureSnapshot& snapshot) {
                if (snapshot.state == CaptureState::Running && snapshot.runningSources == AudioSource::Spk) {
                    survivorReachedRunningInsideIssue = true;
                    nested.quit();
                }
            });
            QTimer::singleShot(2000, &nested, &QEventLoop::quit);
            nested.exec();
        }, Qt::DirectConnection);
        fake.failStartCount = 1;
        QVERIFY(manager.resumeCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(enteredIssue, 3000);
        if (stopFromIssue) {
            QVERIFY(stopResult.accepted);
            QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        } else {
            QVERIFY(survivorReachedRunningInsideIssue);
            QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
            QCOMPARE(manager.capture().runningSources, AudioSources(AudioSource::Spk));
        }

        QTRY_VERIFY_WITH_TIMEOUT(!deviceIsLive.load(std::memory_order_acquire), 3000);
        auto failedMicTails = [&] {
            int count = 0;
            for (const auto& header : consumer.acknowledgedBoundaries)
                if (header.source == std::uint32_t(AudioSource::Mic) &&
                    header.kind == std::uint32_t(BlockKind::StreamEnd) &&
                    header.errorCode == std::uint32_t(AudioErrorCode::BackendStartFailed)) ++count;
            return count;
        };
        QTRY_COMPARE_WITH_TIMEOUT(failedMicTails(), 1, 3000);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
        if (!stopFromIssue) {
            QVERIFY(FakeMacSystemCapture::state().live);
            const auto priorAudioBlocks = consumer.audioBlocks;
            const std::int16_t samples[] = {8192, -8192};
            FakeMacSystemCapture::sendSamples(samples, 1);
            QTRY_COMPARE_WITH_TIMEOUT(consumer.audioBlocks, priorAudioBlocks + 1, 3000);
            QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
            QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        }
        QCOMPARE(failedMicTails(), 1);
        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
    }

    void oneSourceStartupFailureRollsBackBothSources_data()
    {
        QTest::addColumn<bool>("failPreparation");
        QTest::newRow("prepare-failure-with-nested-issue-loop") << true;
        QTest::newRow("start-failure-with-nested-issue-loop") << false;
    }

    void oneSourceStartupFailureRollsBackBothSources()
    {
        using namespace AudioPipline;
        QFETCH(bool, failPreparation);
        prepareInput();
        prepareOutput();
        FakeMacSystemCapture::state().failPrepare = failPreparation;
        FakeMacSystemCapture::state().failStart = !failPreparation;
        AudioPiplineManager manager;
        QSignalSpy startedSignal(&manager, &AudioPiplineManager::captureStarted);
        QSignalSpy issues(&manager, &AudioPiplineManager::issueReported);
        bool enteredIssue = false;
        bool reachedIdleInsideIssue = false;
        connect(&manager, &AudioPiplineManager::issueReported, &manager,
                [&](const AudioIssue& issue) {
            if (enteredIssue || issue.error.source != AudioSource::Spk ||
                issue.error.code != AudioErrorCode::BackendStartFailed) return;
            enteredIssue = true;
            QEventLoop nested;
            connect(&manager, &AudioPiplineManager::captureChanged, &nested,
                    [&](const CaptureSnapshot& snapshot) {
                if (snapshot.state == CaptureState::Idle) {
                    reachedIdleInsideIssue = true;
                    nested.quit();
                }
            });
            QTimer::singleShot(2000, &nested, &QEventLoop::quit);
            nested.exec();
        }, Qt::DirectConnection);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(enteredIssue);
        QVERIFY(reachedIdleInsideIssue);
        QVERIFY(startedSignal.isEmpty());
        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
        QVERIFY(!deviceIsLive.load());
        QVERIFY(!FakeMacSystemCapture::state().live);
        QCOMPARE(fake.deviceStartCalls, failPreparation ? 0 : 1);
        QCOMPARE(fake.deviceStopCalls, failPreparation ? 0 : 1);
        QVERIFY(!issues.isEmpty());
        const auto issue = qvariant_cast<AudioIssue>(issues.front().at(0));
        QCOMPARE(issue.captureId, started.captureId);
        QCOMPARE(issue.error.source, AudioSource::Spk);
        QCOMPARE(issue.error.code, AudioErrorCode::BackendStartFailed);
    }
#endif

    void capturesPcmAndPausesAfterConsumerAcknowledgesBoundaries()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        config.queue.enabledStages = AudioPipline::ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY(fake.callback); QVERIFY(fake.activeDevice);
        const std::int16_t input[] = {8192, -16384, 24576, -32768};
        fake.callback(fake.activeDevice, nullptr, input, 4);
        auto block = manager.pcmQueue().tryAcquire(started.captureId, AudioPipline::Stage::Convert);
        QCOMPARE(block.status, AudioPipline::AcquireStatus::Acquired);
        QVERIFY(block.block);
        QCOMPARE(block.block->header().source, std::uint32_t(AudioPipline::AudioSource::Mic));
        QCOMPARE(block.block->header().validBytes, std::uint32_t(sizeof(input)));
        QCOMPARE(std::memcmp(block.block->data(), input, sizeof(input)), 0);
        AudioPipline::PcmInfo info; info.format = block.block->header().format;
        info.frameCount = 4; info.validBytes = sizeof(input); info.timestampNs = block.block->header().pcmTimestampNs;
        QCOMPARE(manager.pcmQueue().commit(*block.block, info).status, AudioPipline::QueueStatus::Ok);
        ConvertConsumer consumer(manager, started.captureId);
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Paused);
        QCOMPARE(consumer.segmentEnds, 1);
        QVERIFY(manager.resumeCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QCOMPARE(fake.deviceStartCalls, 2);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
        QCOMPARE(consumer.streamEnds, 1);
    }

    void drainCanBeUpgradedToDiscardBeforeConsumerAcknowledgement()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        config.queue.enabledStages = AudioPipline::ProcessingStage::Convert;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        const std::int16_t input[] = {3277, 6554}; fake.callback(fake.activeDevice, nullptr, input, 2);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Drain).accepted);
        QCOMPARE(manager.capture().state, AudioPipline::CaptureState::Stopping);
        QVERIFY(manager.queueSnapshot().usedSlots > 0);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        ConvertConsumer consumer(manager, started.captureId);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
        QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
        QVERIFY(consumer.cancelledBlocks > 0);
        QCOMPARE(consumer.streamEnds, 1);
    }

    void disabledProcessingStagesAllowDrainWithoutAConsumer()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        const std::int16_t samples[] = {8192, -8192};
        fake.callback(fake.activeDevice, nullptr, samples, 2);
        QSignalSpy drained(&manager, &AudioPiplineManager::queueDrained);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
        QCOMPARE(drained.count(), 1);
        const auto result = qvariant_cast<DrainResult>(drained.at(0).at(1));
        QCOMPARE(result.mode, StopMode::Drain);
        QVERIFY(!result.cancelled);
    }

    void cancelledBoundaryWaitsForEnabledConsumer_data()
    {
        QTest::addColumn<bool>("pause");
        QTest::newRow("pause") << true;
        QTest::newRow("stop") << false;
    }

    void cancelledBoundaryWaitsForEnabledConsumer()
    {
        using namespace AudioPipline;
        QFETCH(bool, pause);
        prepareInput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        config.queue.enabledStages = ProcessingStage::Convert;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QSignalSpy drained(&manager, &AudioPiplineManager::queueDrained);
        QVERIFY((pause ? manager.pauseCapture(started.captureId) :
                         manager.stopCapture(started.captureId, StopMode::Discard)).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(!deviceIsLive.load(std::memory_order_acquire), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.queueSnapshot().usedSlots, 1u, 3000);
        auto boundary = manager.pcmQueue().tryAcquire(started.captureId, Stage::Convert);
        QCOMPARE(boundary.status, AcquireStatus::Acquired);
        const auto header = boundary.block->header();
        QCOMPARE(header.kind, std::uint32_t(pause ? BlockKind::SegmentEnd : BlockKind::StreamEnd));
        QVERIFY(header.flags & std::uint32_t(BlockFlag::Cancelled));
        // Let completion events and several queue-pump turns run while the
        // enabled consumer owns the tail; none may bypass its acknowledgement.
        QTest::qWait(50);
        QCOMPARE(manager.capture().state, CaptureState::Stopping);
        QVERIFY(drained.isEmpty());
        QCOMPARE(manager.queueSnapshot().usedSlots, 1u);
        const PcmInfo info{header.format, header.frameCount, header.validBytes, header.pcmTimestampNs};
        QCOMPARE(manager.pcmQueue().commit(*boundary.block, info).status, QueueStatus::Cancelled);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state,
                                  pause ? CaptureState::Paused : CaptureState::Idle, 3000);
        if (pause) {
            QVERIFY(drained.isEmpty());
            ConvertConsumer consumer(manager, started.captureId);
            QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
            QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
            QVERIFY2(consumer.error.isEmpty(), qPrintable(consumer.error));
            QCOMPARE(consumer.streamEnds, 1);
        }
        QCOMPARE(drained.count(), 1);
        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
    }

    void stoppedDeviceFailureCanBeRetried()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        fake.failStopCount = 1;
        QSignalSpy errors(&manager, &AudioPiplineManager::issueReported);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 3000);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
        QVERIFY(fake.deviceStopCalls >= 2);
    }

    void failedShutdownKeepsCallbackContextAlive()
    {
        prepareInput();
        auto manager = std::make_unique<AudioPiplineManager>();
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager->startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager->capture().state == AudioPipline::CaptureState::Running || manager->capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager->capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        const auto callback = fake.callback;
        auto* device = fake.activeDevice;
        fake.failStopCount = 100;
        manager.reset();
        QCOMPARE(fake.uninitCalls, 0);
        const std::int16_t sample = 16384;
        callback(device, nullptr, &sample, 1); // gated; userData must still be valid
    }

    void oldCaptureIdCannotStopANewCapture()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto first = manager.startCapture(config); QVERIFY(first.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY(manager.stopCapture(first.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
        const auto second = manager.startCapture(config); QVERIFY(second.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        const auto stale = manager.stopCapture(first.captureId, AudioPipline::StopMode::Discard);
        QVERIFY(!stale.accepted);
        QCOMPARE(stale.error.code, AudioPipline::AudioErrorCode::StaleCapture);
        QCOMPARE(manager.capture().state, AudioPipline::CaptureState::Running);
        QVERIFY(manager.stopCapture(second.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
    }

    void newCaptureReceivesFramesAfterPreviousFailure()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto first = manager.startCapture(config); QVERIFY(first.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY(fake.notification);
        ma_device_notification old{}; old.pDevice = fake.activeDevice;
        old.type = ma_device_notification_type_stopped;
        fake.notification(&old); // notification handler is queued to the manager thread
        QVERIFY(manager.stopCapture(first.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        const auto second = manager.startCapture(config); QVERIFY(second.accepted);
        QSignalSpy issues(&manager, &AudioPiplineManager::issueReported);
        QSignalSpy updates(&manager, &AudioPiplineManager::queueUpdated);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        for (const auto& record : issues) {
            const auto issue = qvariant_cast<AudioPipline::AudioIssue>(record.at(0));
            QVERIFY(issue.captureId != second.captureId || issue.error.code != AudioPipline::AudioErrorCode::DeviceDisconnected);
        }
        const std::int16_t samples[] = {3277, -3277}; fake.callback(fake.activeDevice, nullptr, samples, 2);
        const quint64 revision = manager.queueSnapshot().revision;
        QTRY_VERIFY([&] {
            for (const auto& record : updates)
                if (record.at(0).toString() == second.captureId && record.at(1).toULongLong() >= revision) return true;
            return false;
        }());
        QVERIFY(manager.stopCapture(second.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY(manager.capture().state == AudioPipline::CaptureState::Idle);
    }

    void stopFromStartingSignalDoesNotStartDevices()
    {
        prepareInput();
        AudioPiplineManager manager;
        connect(&manager, &AudioPiplineManager::captureChanged, &manager,
                [&manager](const AudioPipline::CaptureSnapshot& snapshot) {
            if (snapshot.state == AudioPipline::CaptureState::Starting)
                manager.stopCapture(snapshot.captureId, AudioPipline::StopMode::Discard);
        }, Qt::DirectConnection);
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QCOMPARE(fake.deviceStartCalls, 0);
        QCOMPARE(manager.queueSnapshot().state, AudioPipline::QueueState::Closed);
    }

    void stopFromRunningSignalDoesNotEmitStartedOrReviveCapture()
    {
        prepareInput();
        AudioPiplineManager manager;
        QSignalSpy startedSignal(&manager, &AudioPiplineManager::captureStarted);
        connect(&manager, &AudioPiplineManager::captureChanged, &manager,
                [&manager](const AudioPipline::CaptureSnapshot& snapshot) {
            if (snapshot.state == AudioPipline::CaptureState::Running)
                manager.stopCapture(snapshot.captureId, AudioPipline::StopMode::Discard);
        }, Qt::DirectConnection);
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QCOMPARE(fake.deviceStartCalls, 1);
        QCOMPARE(fake.deviceStopCalls, 1);
        QVERIFY(startedSignal.isEmpty());
        QVERIFY(!fake.activeDevice);
    }

    void stopFromResumeStartingSignalCannotRestartDevices()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Paused, 3000);
        connect(&manager, &AudioPiplineManager::captureChanged, &manager,
                [&manager](const AudioPipline::CaptureSnapshot& snapshot) {
            if (snapshot.state == AudioPipline::CaptureState::Starting)
                manager.stopCapture(snapshot.captureId, AudioPipline::StopMode::Discard);
        }, Qt::DirectConnection);
        QVERIFY(manager.resumeCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QCOMPARE(fake.deviceStartCalls, 1);
    }

    void slowDeviceInitializationAndStartDoNotBlockEventLoop()
    {
        prepareInput();
        initDelayMs = 300;
        startDelayMs = 300;
        QObject signalObserver;
        std::atomic<bool> wrongSignalThread{false};
        AudioPiplineManager manager;
        connect(&manager, &AudioPiplineManager::captureChanged, &signalObserver,
                [&](const AudioPipline::CaptureSnapshot&) {
            if (QThread::currentThread() != manager.thread()) wrongSignalThread.store(true);
        }, Qt::DirectConnection);
        bool tickedDuringInit = false;
        bool tickedDuringStart = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            tickedDuringInit |= initInProgress.load(std::memory_order_acquire);
            tickedDuringStart |= startInProgress.load(std::memory_order_acquire);
        });
        heartbeat.start(5);
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running ||
                                 manager.capture().state == AudioPipline::CaptureState::Idle, 4000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY2(tickedDuringInit, "The manager event loop blocked during device initialization");
        QVERIFY2(tickedDuringStart, "The manager event loop blocked during device start");
        QVERIFY2(!wrongSignalThread.load(), "Capture state was emitted outside the manager thread");
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(!wrongSignalThread.load(), "Capture state was emitted outside the manager thread");
    }

    void slowDeviceStopDoesNotBlockEventLoop()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running ||
                                 manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        stopDelayMs = 300;
        bool tickedDuringStop = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            tickedDuringStop |= stopInProgress.load(std::memory_order_acquire);
        });
        heartbeat.start(5);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(tickedDuringStop, "The manager event loop blocked during device stop");
    }

    void stopDuringPendingPauseFinishesCaptureExactlyOnce()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);

        QSignalSpy stopped(&manager, &AudioPiplineManager::captureStopped);
        QSignalSpy drained(&manager, &AudioPiplineManager::queueDrained);
        QSignalSpy changes(&manager, &AudioPiplineManager::captureChanged);
        stopDelayMs = 300;
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(stopInProgress.load(std::memory_order_acquire), 3000);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);

        QCOMPARE(manager.queueSnapshot().state, QueueState::Closed);
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).toString(), started.captureId);
        QCOMPARE(drained.count(), 1);
        QCOMPARE(drained.at(0).at(0).toString(), started.captureId);
        QCOMPARE(qvariant_cast<DrainResult>(drained.at(0).at(1)).mode, StopMode::Discard);
        for (const auto& change : changes)
            QVERIFY(qvariant_cast<CaptureSnapshot>(change.at(0)).state != CaptureState::Paused);
    }

    void nestedStoppedSignalCannotOverwriteNewCapture()
    {
        prepareInput();
        AudioPiplineManager manager;
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto first = manager.startCapture(config); QVERIFY(first.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running ||
                                 manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        AudioPipline::StartResult second;
        bool entered = false;
        connect(&manager, &AudioPiplineManager::captureStopped, &manager,
                [&](const AudioPipline::CaptureId& id) {
            if (id != first.captureId || entered) return;
            entered = true;
            QEventLoop nested;
            QTimer poll;
            connect(&poll, &QTimer::timeout, &nested, [&] {
                if (manager.capture().state != AudioPipline::CaptureState::Idle) return;
                second = manager.startCapture(config);
                nested.quit();
            });
            QTimer::singleShot(2000, &nested, &QEventLoop::quit);
            poll.start(1);
            nested.exec();
        }, Qt::DirectConnection);
        QVERIFY(manager.stopCapture(first.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(entered && second.accepted, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running, 3000);
        QCOMPARE(manager.capture().captureId, second.captureId);
        QVERIFY(manager.stopCapture(second.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
    }

    void slowDeviceEnumerationDuringStartDoesNotBlockEventLoop()
    {
        prepareInput();
        AudioPiplineManager manager;
        // The constructor's initial enumeration is covered by the legacy API;
        // this checks the subsequent enumeration performed during startup.
        enumerateDelayMs = 300;
        bool tickedDuringEnumeration = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            tickedDuringEnumeration |= enumerateInProgress.load(std::memory_order_acquire);
        });
        heartbeat.start(5);
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running ||
                                 manager.capture().state == AudioPipline::CaptureState::Idle, 4000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        QVERIFY2(tickedDuringEnumeration, "The manager event loop blocked during device enumeration");
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
    }

    void slowDeviceRefreshDoesNotBlockEventLoop()
    {
        prepareInput();
        AudioPiplineManager manager;
        enumerateDelayMs = 300;
        bool tickedDuringEnumeration = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            tickedDuringEnumeration |= enumerateInProgress.load(std::memory_order_acquire);
        });
        heartbeat.start(5);
        QSignalSpy finished(&manager, &AudioPiplineManager::requestFinished);
        const auto request = manager.refreshDevices();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 4000);
        QCOMPARE(finished.at(0).at(0).toString(), request);
        const auto result = qvariant_cast<AudioPipline::RequestResult>(finished.at(0).at(1));
        QCOMPARE(result.kind, AudioPipline::RequestKind::RefreshDevices);
        QCOMPARE(result.status, AudioPipline::RequestStatus::Completed);
        QVERIFY2(tickedDuringEnumeration, "The manager event loop blocked during device refresh");
    }

    void cancellationDuringInitializationWaitsForDeviceCleanup()
    {
        prepareInput();
        initDelayMs = 400;
        AudioPiplineManager manager;
        QSignalSpy startedSignal(&manager, &AudioPiplineManager::captureStarted);
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager.startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(initInProgress.load(std::memory_order_acquire), 3000);
        QVERIFY(manager.stopCapture(started.captureId, AudioPipline::StopMode::Discard).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY(!initInProgress.load(std::memory_order_acquire));
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QVERIFY(startedSignal.isEmpty());
    }

    void cancelledResumeCannotPublishLateStartIntoNextCapture()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        const auto first = manager.startCapture(config);
        QVERIFY(first.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QVERIFY(manager.pauseCapture(first.captureId).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);

        QSignalSpy startedSignals(&manager, &AudioPiplineManager::captureStarted);
        startDelayMs = 300;
        QVERIFY(manager.resumeCapture(first.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(startInProgress.load(std::memory_order_acquire), 3000);
        QVERIFY(manager.stopCapture(first.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(startedSignals.isEmpty());

        startDelayMs = 0;
        const auto second = manager.startCapture(config);
        QVERIFY(second.accepted);
        QVERIFY(second.captureId != first.captureId);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QTest::qWait(50);
        QCOMPARE(manager.capture().state, CaptureState::Running);
        QCOMPARE(manager.capture().captureId, second.captureId);
        QCOMPARE(startedSignals.count(), 1);
        QCOMPARE(startedSignals.at(0).at(0).toString(), second.captureId);
        const std::int16_t sample = 16384;
        fake.callback(fake.activeDevice, nullptr, &sample, 1);
        QCOMPARE(manager.queueSnapshot().writeTicket, 1u);
        QVERIFY(manager.stopCapture(second.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
    }

    void destructionDuringInitializationWaitsForDeviceCleanup()
    {
        prepareInput();
        initDelayMs = 400;
        auto manager = std::make_unique<AudioPiplineManager>();
        AudioPipline::CaptureConfig config; config.sources = AudioPipline::AudioSource::Mic;
        const auto started = manager->startCapture(config); QVERIFY(started.accepted);
        QTRY_VERIFY_WITH_TIMEOUT(initInProgress.load(std::memory_order_acquire), 3000);
        manager.reset();
        QVERIFY(!initInProgress.load(std::memory_order_acquire));
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QCOMPARE(fake.uninitCalls, 1);
    }
};

QTEST_GUILESS_MAIN(AudioPipelineManagerTests)
#include "audio_pipline_manager_tests.moc"
