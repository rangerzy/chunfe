#include "audio_pipline_manager.h"
#include "audio_level_test.h"
#include "captures/audio_capture_buffer.h"
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
#include <algorithm>
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

// Observe the production handoff on the manager's thread so the tests can
// check payloads and ordering across capture and stop signals.
class PcmObserver final : public QObject
{
public:
    explicit PcmObserver(AudioPiplineManager& manager)
    {
        connect(&manager, &AudioPiplineManager::pcmReady, this,
                [this](const CaptureId& id, std::uint32_t epoch, AudioSource source,
                       const CapturedAudio& audio) {
            events.append({id, epoch, source, audio, 0, false});
        });
        connect(&manager, &AudioPiplineManager::pcmEnded, this,
                [this](const CaptureId& id, std::uint32_t epoch, AudioSource source,
                       std::uint64_t trailingDroppedFrames) {
            events.append({id, epoch, source, {}, trailingDroppedFrames, true});
        });
    }

    using CaptureId = AudioPipline::CaptureId;
    using AudioSource = AudioPipline::AudioSource;
    using CapturedAudio = AudioPipline::CapturedAudio;
    struct Event {
        CaptureId id;
        std::uint32_t epoch;
        AudioSource source;
        CapturedAudio audio;
        std::uint64_t trailingDroppedFrames;
        bool ended;
    };

    QList<Event> events;

    int countEnds(AudioSource source, std::uint32_t epoch) const
    {
        int count = 0;
        for (const auto& event : events)
            if (event.ended && event.source == source && event.epoch == epoch) ++count;
        return count;
    }
};

bool waitForCaptureActivation(AudioPiplineManager& manager)
{
    // Running is emitted before activation is posted to the device thread.
    // The following level update is only possible once that thread activated.
    QSignalSpy changes(&manager, &AudioPiplineManager::captureChanged);
    return changes.wait(1000) && manager.capture().state == AudioPipline::CaptureState::Running;
}
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
        auto buffer = std::make_shared<AudioCaptureBuffer>();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        AudioCaptureWorker worker(AudioSource::Mic, context);
        AudioCaptureWorker::Output output{buffer, id, std::chrono::steady_clock::now() - std::chrono::seconds(1), 1};
        QSignalSpy wake(&worker, &AudioCaptureWorker::pcmAvailable);
        QCOMPARE(worker.prepare({}, output, false).code, AudioErrorCode::None);
        QCOMPARE(worker.startCapture().code, AudioErrorCode::None);

        std::int16_t samples[325];
        std::fill(std::begin(samples), std::end(samples), 16384);
        fake.callback(fake.activeDevice, nullptr, samples, 325);
        CapturedAudio audio;
        QVERIFY(!buffer->tryTake(audio));
        worker.setAccepting(true);
        fake.callback(fake.activeDevice, nullptr, samples, 325);
        // Producer retains its own copy until the reader consumes each slot.
        std::fill(std::begin(samples), std::end(samples), -16384);
        QCOMPARE(wake.count(), 1);
        QCOMPARE(fake.requestedFormat, ma_format_s16);
        QCOMPARE(fake.requestedRate, 16000u);
        QCOMPARE(fake.requestedChannels, 1u);
        QCOMPARE(worker.level().peak, 0.5f);
        QCOMPARE(worker.level().rms, 0.5f);
        QVERIFY(buffer->tryTake(audio));
        QCOMPARE(audio.sequence, quint64(0));
        QCOMPARE(audio.pcm.size(), 640);
        const auto firstTimestamp = audio.timestampNs;
        QVERIFY(buffer->tryTake(audio));
        QCOMPARE(audio.sequence, quint64(1));
        QCOMPARE(audio.timestampNs - firstTimestamp, qint64(20000000));
        QCOMPARE(audio.pcm.size(), 10);
        const std::int16_t copied[] = {16384, 16384, 16384, 16384, 16384};
        QCOMPARE(std::memcmp(audio.pcm.constData(), copied, sizeof(copied)), 0);

        fake.emitFrameOnStop = true;
        QCOMPARE(worker.stopCapture().code, AudioErrorCode::None);
        QVERIFY(!buffer->tryTake(audio));
        QCOMPARE(wake.count(), 1);
        QVERIFY(!deviceIsLive.load());
    }

    void workerResumeDoesNotSwitchToNewDefaultDevice()
    {
        using namespace AudioPipline;
        prepareInput();
        auto context = std::make_shared<AudioCaptureContext>();
        context->refresh();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        AudioCaptureWorker worker(AudioSource::Mic, context);
        AudioCaptureWorker::Output output{nullptr, id, std::chrono::steady_clock::now(), 1};
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
        auto buffer = std::make_shared<AudioCaptureBuffer>();
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        AudioCaptureWorker worker(AudioSource::Spk, context);
        AudioCaptureWorker::Output output{buffer, id, std::chrono::steady_clock::now() - std::chrono::seconds(1), 1};
        QCOMPARE(worker.prepare({}, output, false).code, AudioErrorCode::None);
        QCOMPARE(worker.startCapture().code, AudioErrorCode::None);
        worker.setAccepting(true);

        // Simulate two 64 ms outputs from the same original input.
        const std::int16_t samples[1024] = {};
        FakeMacSystemCapture::sendSamples(samples, 1024, 64000000);
        FakeMacSystemCapture::sendSamples(samples, 1024);
        CapturedAudio audio;
        QVERIFY(buffer->tryTake(audio));
        const auto firstTimestamp = audio.timestampNs;
        for (int i = 0; i < 4; ++i)
            QVERIFY(buffer->tryTake(audio));
        const auto delta = audio.timestampNs - firstTimestamp;
        QVERIFY(delta >= 64000000 && delta < 264000000);
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

    void systemDeviceDisconnectedDuringStartupPromptsReselection()
    {
        using namespace AudioPipline;
        prepareInput();
        prepareOutput();
        FakeMacSystemCapture::state().disconnectOnStart = true;
        AudioPiplineManager manager;
        AudioLevelTest meter(manager);
        QSignalSpy started(&manager, &AudioPiplineManager::captureStarted);
        QSignalSpy issues(&manager, &AudioPiplineManager::issueReported);

        meter.start(QStringLiteral("fake-microphone"), QStringLiteral("fake-speaker"));

        QTRY_COMPARE_WITH_TIMEOUT(issues.size(), 1, 3000);
        const auto issue = qvariant_cast<AudioIssue>(issues.front().at(0));
        QCOMPARE(issue.error.source, AudioSource::Spk);
        QCOMPARE(issue.error.code, AudioErrorCode::DeviceDisconnected);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(meter.systemStatus(), issue.error.message);
        QVERIFY(meter.systemStatus().contains(QStringLiteral("请重新选择输出设备")));
        QVERIFY(!meter.active());
        QVERIFY(started.isEmpty());
        QCOMPARE(fake.deviceStartCalls, 1);
        QCOMPARE(FakeMacSystemCapture::state().startCalls, 1);
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
        PcmObserver observer(manager);
        bool observedSystemStartup = false;
        bool publishedBeforeReady = false;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            if (!system.startInProgress.load(std::memory_order_acquire)) return;
            observedSystemStartup = true;
            publishedBeforeReady |= manager.capture().state != CaptureState::Starting ||
                                    !startedSignal.isEmpty() || !observer.events.isEmpty();
        });
        heartbeat.start(5);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.deliverPcm = true;
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
        QVERIFY(waitForCaptureActivation(manager));
        QVERIFY(observer.events.isEmpty());

        const std::int16_t mic[] = {16384, -16384};
        const std::int16_t spk[] = {8192, -8192};
        fake.callback(fake.activeDevice, nullptr, mic, 2);
        FakeMacSystemCapture::sendSamples(spk, 2);
        QTRY_COMPARE_WITH_TIMEOUT(observer.events.size(), 2, 3000);
        AudioSources received;
        for (const auto& event : observer.events) {
            QCOMPARE(event.id, started.captureId);
            QCOMPARE(event.epoch, 0u);
            QVERIFY(!event.ended);
            QVERIFY(!received.testFlag(event.source));
            received |= event.source;
            QCOMPARE(event.audio.sequence, quint64(0));
            QCOMPARE(event.audio.pcm.size(), 4);
            QCOMPARE(std::memcmp(event.audio.pcm.constData(), event.source == AudioSource::Mic ? mic : spk, 4), 0);
        }
        QCOMPARE(received, AudioSource::Mic | AudioSource::Spk);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(observer.countEnds(AudioSource::Mic, 0), 1);
        QCOMPARE(observer.countEnds(AudioSource::Spk, 0), 1);
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
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        PcmObserver observer(manager);
        QTRY_VERIFY_WITH_TIMEOUT(disconnectedDuringSystemStartup, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(startedSignals.isEmpty());
        QVERIFY(observer.events.isEmpty());
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QVERIFY(!system.live);
    }

    void sourceFailureDuringPauseStopsBothDevices_data()
    {
        QTest::addColumn<bool>("pauseDuringStopRetry");
        QTest::newRow("pause-from-disconnection-issue") << false;
        QTest::newRow("pause-during-failed-source-stop-retry") << true;
    }

    void sourceFailureDuringPauseStopsBothDevices()
    {
        using namespace AudioPipline;
        QFETCH(bool, pauseDuringStopRetry);
        const auto failedSource = pauseDuringStopRetry ? AudioSource::Spk : AudioSource::Mic;
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.deliverPcm = true;
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

        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);
        QVERIFY(!deviceIsLive.load(std::memory_order_acquire));
        QVERIFY(!FakeMacSystemCapture::state().live);
        QVERIFY(observer.events.isEmpty());
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(observer.events.isEmpty());
    }

    void resumeFailureKeepsSurvivorUsableAcrossIssueReentry_data()
    {
        QTest::addColumn<bool>("stopFromIssue");
        QTest::newRow("nested-event-loop-until-survivor-running") << false;
        QTest::newRow("stop-directly-from-resume-failure-issue") << true;
    }

    void resumeFailureKeepsSurvivorUsableAcrossIssueReentry()
    {
        using namespace AudioPipline;
        QFETCH(bool, stopFromIssue);
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        PcmObserver observer(manager);
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);
        QVERIFY(observer.events.isEmpty());

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
        QVERIFY(observer.events.isEmpty());
        if (!stopFromIssue) {
            QVERIFY(FakeMacSystemCapture::state().live);
            QVERIFY(waitForCaptureActivation(manager));
            const std::int16_t samples[] = {8192, -8192};
            FakeMacSystemCapture::sendSamples(samples, 1);
            QTRY_COMPARE_WITH_TIMEOUT(observer.events.size(), 1, 3000);
            const auto& event = observer.events.last();
            QVERIFY(!event.ended);
            QCOMPARE(event.source, AudioSource::Spk);
            QCOMPARE(event.epoch, 1u);
            QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
            QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
            QCOMPARE(observer.events.size(), 1);
        }
    }

    void runtimeFailureDeliversTailAndKeepsOtherSourceRunning()
    {
        using namespace AudioPipline;
        prepareInput();
        prepareOutput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic | AudioSource::Spk;
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QVERIFY(waitForCaptureActivation(manager));

        const std::int16_t tail[] = {8192, -8192};
        fake.callback(fake.activeDevice, nullptr, tail, 2);
        ma_device_notification disconnected{};
        disconnected.pDevice = fake.activeDevice;
        disconnected.type = ma_device_notification_type_stopped;
        fake.notification(&disconnected);
        QTRY_COMPARE_WITH_TIMEOUT(observer.countEnds(AudioSource::Mic, 0), 1, 3000);
        QCOMPARE(manager.capture().state, CaptureState::Running);
        QCOMPARE(manager.capture().runningSources, AudioSources(AudioSource::Spk));
        QCOMPARE(observer.events.size(), 2);
        QCOMPARE(observer.events.first().source, AudioSource::Mic);
        QVERIFY(!observer.events.first().ended);
        QCOMPARE(observer.events.first().audio.pcm,
                 QByteArray(reinterpret_cast<const char*>(tail), sizeof(tail)));
        QVERIFY(observer.events.last().ended);

        const std::int16_t survivor[] = {-4096, 4096};
        FakeMacSystemCapture::sendSamples(survivor, 2);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(observer.events.size(), 4);
        QCOMPARE(observer.events.at(2).source, AudioSource::Spk);
        QVERIFY(!observer.events.at(2).ended);
        QCOMPARE(observer.events.at(2).audio.pcm,
                 QByteArray(reinterpret_cast<const char*>(survivor), sizeof(survivor)));
        QCOMPARE(observer.countEnds(AudioSource::Mic, 0), 1);
        QCOMPARE(observer.countEnds(AudioSource::Spk, 0), 1);
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

    void drainDeliversQueuedTailBeforeEnd()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QVERIFY(fake.callback && fake.activeDevice);
        QVERIFY(waitForCaptureActivation(manager));

        // Both callbacks return before the manager's event loop can deliver them.
        const std::int16_t first[] = {8192, -16384, 24576, -32768};
        const std::int16_t tail[] = {4096};
        fake.callback(fake.activeDevice, nullptr, first, 4);
        fake.callback(fake.activeDevice, nullptr, tail, 1);
        QVERIFY(observer.events.isEmpty());
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(observer.events.size(), 3);
        for (const auto& event : observer.events) {
            QCOMPARE(event.id, started.captureId);
            QCOMPARE(event.source, AudioSource::Mic);
            QCOMPARE(event.epoch, 0u);
        }
        QVERIFY(!observer.events.at(0).ended);
        QCOMPARE(observer.events.at(0).audio.sequence, quint64(0));
        QCOMPARE(observer.events.at(0).audio.pcm, QByteArray(reinterpret_cast<const char*>(first), sizeof(first)));
        QVERIFY(!observer.events.at(1).ended);
        QCOMPARE(observer.events.at(1).audio.sequence, quint64(1));
        QCOMPARE(observer.events.at(1).audio.pcm, QByteArray(reinterpret_cast<const char*>(tail), sizeof(tail)));
        QVERIFY(observer.events.at(2).ended);
        QCOMPARE(observer.events.at(2).trailingDroppedFrames, std::uint64_t(0));
    }

    void pauseDiscardsQueuedPcmAndResumeBeginsNewEpoch()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QVERIFY(waitForCaptureActivation(manager));
        const std::int16_t stale[] = {8192, -8192};
        fake.callback(fake.activeDevice, nullptr, stale, 2);
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Paused, 3000);
        QVERIFY(observer.events.isEmpty());

        QVERIFY(manager.resumeCapture(started.captureId).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QCOMPARE(fake.deviceStartCalls, 2);
        QVERIFY(waitForCaptureActivation(manager));
        const std::int16_t fresh[] = {-4096, 4096};
        fake.callback(fake.activeDevice, nullptr, fresh, 2);
        QTRY_COMPARE_WITH_TIMEOUT(observer.events.size(), 1, 3000);
        const auto event = observer.events.first();
        QCOMPARE(event.id, started.captureId);
        QCOMPARE(event.epoch, 1u);
        QCOMPARE(event.audio.sequence, quint64(0));
        QCOMPARE(event.audio.pcm, QByteArray(reinterpret_cast<const char*>(fresh), sizeof(fresh)));
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QCOMPARE(observer.countEnds(AudioSource::Mic, 0), 0);
        QCOMPARE(observer.countEnds(AudioSource::Mic, 1), 1);
    }

    void drainCanBeUpgradedToDiscardBeforeDelivery()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        config.deliverPcm = true;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QVERIFY(waitForCaptureActivation(manager));
        const std::int16_t input[] = {3277, 6554};
        fake.callback(fake.activeDevice, nullptr, input, 2);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QCOMPARE(manager.capture().state, CaptureState::Stopping);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(observer.events.isEmpty());
    }

    void levelOnlyCaptureDoesNotDeliverPcm()
    {
        using namespace AudioPipline;
        prepareInput();
        AudioPiplineManager manager;
        PcmObserver observer(manager);
        CaptureConfig config;
        config.sources = AudioSource::Mic;
        const auto started = manager.startCapture(config);
        QVERIFY(started.accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        const std::int16_t samples[] = {16384, -16384};
        QTimer inject;
        connect(&inject, &QTimer::timeout, this, [&] {
            fake.callback(fake.activeDevice, nullptr, samples, 2);
        });
        inject.start(10);
        const auto levelVisible = [&] {
            for (const auto& channel : manager.capture().channels)
                if (channel.source == AudioSource::Mic && channel.peak == 0.5f) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(levelVisible(), 3000);
        inject.stop();
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Drain).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);
        QVERIFY(observer.events.isEmpty());
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
        config.deliverPcm = true;
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
        PcmObserver observer(manager);
        QTRY_VERIFY_WITH_TIMEOUT(manager.capture().state == AudioPipline::CaptureState::Running || manager.capture().state == AudioPipline::CaptureState::Idle, 3000);
        QVERIFY2(manager.capture().state == AudioPipline::CaptureState::Running, "Capture startup failed");
        for (const auto& record : issues) {
            const auto issue = qvariant_cast<AudioPipline::AudioIssue>(record.at(0));
            QVERIFY(issue.captureId != second.captureId || issue.error.code != AudioPipline::AudioErrorCode::DeviceDisconnected);
        }
        QVERIFY(waitForCaptureActivation(manager));
        const std::int16_t samples[] = {3277, -3277}; fake.callback(fake.activeDevice, nullptr, samples, 2);
        QTRY_COMPARE_WITH_TIMEOUT(observer.events.size(), 1, 3000);
        QCOMPARE(observer.events.first().id, second.captureId);
        QCOMPARE(observer.events.first().audio.sequence, quint64(0));
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
        QSignalSpy changes(&manager, &AudioPiplineManager::captureChanged);
        stopDelayMs = 300;
        QVERIFY(manager.pauseCapture(started.captureId).accepted);
        QTRY_VERIFY_WITH_TIMEOUT(stopInProgress.load(std::memory_order_acquire), 3000);
        QVERIFY(manager.stopCapture(started.captureId, StopMode::Discard).accepted);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Idle, 3000);

        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).toString(), started.captureId);
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
        config.deliverPcm = true;
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
        PcmObserver observer(manager);
        const auto second = manager.startCapture(config);
        QVERIFY(second.accepted);
        QVERIFY(second.captureId != first.captureId);
        QTRY_COMPARE_WITH_TIMEOUT(manager.capture().state, CaptureState::Running, 3000);
        QTest::qWait(50);
        QCOMPARE(manager.capture().state, CaptureState::Running);
        QCOMPARE(manager.capture().captureId, second.captureId);
        QCOMPARE(startedSignals.count(), 1);
        QCOMPARE(startedSignals.at(0).at(0).toString(), second.captureId);
        QVERIFY(waitForCaptureActivation(manager));
        const std::int16_t sample = 16384;
        fake.callback(fake.activeDevice, nullptr, &sample, 1);
        QTRY_COMPARE_WITH_TIMEOUT(observer.events.size(), 1, 3000);
        QCOMPARE(observer.events.first().id, second.captureId);
        QCOMPARE(observer.events.first().audio.sequence, quint64(0));
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
