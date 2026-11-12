#include "audio_level_test.h"

#include <QtTest>

using namespace AudioPipline;

namespace {
struct FakeManager {
    CaptureSnapshot snapshot;
    QList<CaptureConfig> starts;
    QList<CaptureId> stops;
    QList<RequestId> cancels;
    int permissions = 0;
    int nextCapture = 0;
    bool accept = true;
    bool acceptStop = true;
} fake;

void requestFinished(AudioPiplineManager& manager, PermissionState state)
{
    RequestResult result;
    result.kind = RequestKind::RequestPermissions;
    result.status = RequestStatus::Completed;
    PermissionInfo permission;
    permission.source = AudioSource::Mic;
    permission.state = state;
    result.permissions.append(permission);
    emit manager.requestFinished(QStringLiteral("permission-%1").arg(fake.permissions), result);
}

ChannelStatus channel(AudioSource source, ChannelState state, float peak = 0, QString message = {})
{
    ChannelStatus result;
    result.source = source;
    result.state = state;
    result.peak = peak;
    result.error.source = source;
    result.error.message = std::move(message);
    return result;
}

void snapshot(AudioPiplineManager& manager, CaptureState state, QList<ChannelStatus> channels = {})
{
    fake.snapshot.state = state;
    fake.snapshot.channels = std::move(channels);
    emit manager.captureChanged(fake.snapshot);
}

void issue(AudioPiplineManager& manager, AudioSource source, AudioErrorCode code, const QString& message)
{
    AudioIssue result;
    result.captureId = fake.snapshot.captureId;
    result.error = {code, message, source};
    emit manager.issueReported(result);
}

void started(AudioPiplineManager& manager)
{
    emit manager.captureStarted(fake.snapshot.captureId, {});
}
}

// Link this test against the manager header's moc, not the real backend. The
// controlled fake gives each asynchronous receipt a deterministic ordering.
AudioPiplineManager::AudioPiplineManager(QObject* parent) : QObject(parent) {}
AudioPiplineManager::~AudioPiplineManager() = default;
CaptureSnapshot AudioPiplineManager::capture() const { return fake.snapshot; }
RequestId AudioPiplineManager::requestPermissions(AudioSources) { return QStringLiteral("permission-%1").arg(++fake.permissions); }
bool AudioPiplineManager::cancelRequest(const RequestId& id) { fake.cancels.append(id); return true; }
StartResult AudioPiplineManager::startCapture(const CaptureConfig& config)
{
    fake.starts.append(config);
    if (!fake.accept) return {false, {}, {AudioErrorCode::Busy, QStringLiteral("busy")}};
    fake.snapshot = {};
    fake.snapshot.captureId = QStringLiteral("capture-%1").arg(++fake.nextCapture);
    fake.snapshot.state = CaptureState::Starting;
    return {true, fake.snapshot.captureId, {}};
}
CommandResult AudioPiplineManager::stopCapture(const CaptureId& id, StopMode mode)
{
    if (mode != StopMode::Discard) return {false, {AudioErrorCode::InvalidArgument}};
    fake.stops.append(id);
    if (!fake.acceptStop) return {false, {AudioErrorCode::StaleCapture, QStringLiteral("停止失败")}};
    fake.snapshot.state = CaptureState::Stopping;
    return {true, {}};
}

class AudioLevelTestTests : public QObject
{
    Q_OBJECT
private slots:
    void init() { fake = {}; }

    void selectedSourcesProduceRealLevelsAndStop()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        QVERIFY(test.active());
        QCOMPARE(fake.permissions, 1);
        QVERIFY(fake.starts.isEmpty());
        requestFinished(manager, PermissionState::Granted);
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(fake.starts[0].sources, AudioSource::Mic | AudioSource::Spk);
        QCOMPARE(fake.starts[0].mic.deviceId, QStringLiteral("mic"));
        QCOMPARE(fake.starts[0].spk.deviceId, QStringLiteral("speaker"));
        QVERIFY(!fake.starts[0].queue.enabledStages);
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Mic, ChannelState::Running, .25f),
                                                  channel(AudioSource::Spk, ChannelState::Running, 1.5f)});
        started(manager);
        QCOMPARE(test.microphoneLevel(), .25);
        QCOMPARE(test.systemLevel(), 1.0);
        QVERIFY(test.active());
        test.stop();
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        QCOMPARE(test.microphoneStatus(), QStringLiteral("正在停止"));
        QCOMPARE(test.systemStatus(), QStringLiteral("正在停止"));
        QCOMPARE(test.microphoneLevel(), 0.0);
        QCOMPARE(test.systemLevel(), 0.0);
        QCOMPARE(fake.stops, QList<CaptureId>{QStringLiteral("capture-1")});
        test.stop();
        QCOMPARE(fake.stops.size(), 1);
        snapshot(manager, CaptureState::Idle);
        QVERIFY(!test.stopping());
        QCOMPARE(test.microphoneStatus(), QStringLiteral("已停止"));
    }

    void noDevicesAndBusyDoNotTouchOtherSession()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start({}, {});
        QVERIFY(!test.active());
        QCOMPARE(test.microphoneStatus(), QStringLiteral("无设备"));
        QCOMPARE(test.systemStatus(), QStringLiteral("无设备"));
        QCOMPARE(fake.permissions, 0);
        fake.snapshot.captureId = QStringLiteral("other");
        fake.snapshot.state = CaptureState::Running;
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        QVERIFY(!test.active());
        QVERIFY(fake.starts.isEmpty());
        test.stop();
        QVERIFY(fake.stops.isEmpty());
    }

    void cancelPermissionIgnoresLateReply()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        test.stop();
        QCOMPARE(fake.cancels, QList<RequestId>{QStringLiteral("permission-1")});
        requestFinished(manager, PermissionState::Granted);
        QVERIFY(fake.starts.isEmpty());
        QVERIFY(fake.stops.isEmpty());
        QVERIFY(!test.active());
    }

    void oldPermissionCannotStartTheNextTest()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("first"), {});
        test.stop();
        test.start(QStringLiteral("second"), {});
        RequestResult old;
        old.kind = RequestKind::RequestPermissions;
        old.status = RequestStatus::Completed;
        emit manager.requestFinished(QStringLiteral("permission-1"), old);
        QVERIFY(fake.starts.isEmpty());
        requestFinished(manager, PermissionState::Granted);
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(fake.starts.first().mic.deviceId, QStringLiteral("second"));
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void deniedMicAllowsSystemAlone()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Denied);
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(fake.starts[0].sources, AudioSources(AudioSource::Spk));
        QCOMPARE(test.microphoneStatus(), QStringLiteral("麦克风权限未授权，请在系统设置中允许使用麦克风"));
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Spk, ChannelState::Running, .5f)});
        started(manager);
        QCOMPARE(test.systemLevel(), .5);
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void permissionErrorsUseChineseGuidance()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        RequestResult denied;
        denied.kind = RequestKind::RequestPermissions;
        denied.status = RequestStatus::Failed;
        denied.error = {AudioErrorCode::PermissionDenied, QStringLiteral("Permission denied"), AudioSource::Mic};
        emit manager.requestFinished(QStringLiteral("permission-1"), denied);
        QCOMPARE(test.microphoneStatus(), QStringLiteral("麦克风权限未授权，请在系统设置中允许使用麦克风"));
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(fake.starts[0].sources, AudioSources(AudioSource::Spk));
        test.stop();
        snapshot(manager, CaptureState::Idle);

        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Spk, AudioErrorCode::PermissionDenied, QStringLiteral("Access denied"));
        QCOMPARE(test.systemStatus(), QStringLiteral("系统音频权限未授权，请在系统设置中允许录制系统音频"));
        snapshot(manager, CaptureState::Idle);
        QTRY_COMPARE(fake.starts.size(), 3);
        QCOMPARE(fake.starts.last().sources, AudioSources(AudioSource::Mic));
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void failedFallbackDoesNotRetryForever()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStartFailed, QStringLiteral("mic failed"));
        snapshot(manager, CaptureState::Idle);
        QTRY_COMPARE(fake.starts.size(), 2);
        issue(manager, AudioSource::Spk, AudioErrorCode::BackendStartFailed, QStringLiteral("speaker failed"));
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(fake.starts.size(), 2);
        QVERIFY(!test.active());
        QCOMPARE(test.microphoneStatus(), QStringLiteral("mic failed"));
        QCOMPARE(test.systemStatus(), QStringLiteral("speaker failed"));
    }

    void failureDuringStartupRetriesOnlyHealthySource()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        snapshot(manager, CaptureState::Stopping, {channel(AudioSource::Mic, ChannelState::Failed, 0, QStringLiteral("mic failed")),
                                                   channel(AudioSource::Spk, ChannelState::Starting)});
        QVERIFY(test.stopping());
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStartFailed, QStringLiteral("mic failed"));
        QCOMPARE(fake.starts.size(), 1);
        snapshot(manager, CaptureState::Idle);
        QVERIFY(!test.stopping());
        QTRY_COMPARE(fake.starts.size(), 2);
        QCOMPARE(fake.starts[1].sources, AudioSources(AudioSource::Spk));
        QCOMPARE(test.microphoneStatus(), QStringLiteral("mic failed"));
        QVERIFY(test.active());
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Spk, ChannelState::Running, .5f)});
        started(manager);
        QCOMPARE(test.systemLevel(), .5);
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void fallbackDoesNotMutateIdleDuringSignalDelivery()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        CaptureId observedId;
        CaptureState observedState = CaptureState::Starting;
        connect(&manager, &AudioPiplineManager::captureChanged, &manager,
                [&](const CaptureSnapshot& current) {
            if (current.state == CaptureState::Idle) {
                observedId = current.captureId;
                observedState = manager.capture().state;
            }
        });
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStartFailed, QStringLiteral("mic failed"));
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(observedId, QStringLiteral("capture-1"));
        QCOMPARE(observedState, CaptureState::Idle);
        QCOMPARE(fake.starts.size(), 1);
        QTRY_COMPARE(fake.starts.size(), 2);
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void pendingFallbackIsCancelledByLaterIntent_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("stop") << 0;
        QTest::newRow("change-device") << 1;
        QTest::newRow("stop-and-start-new-test") << 2;
    }

    void pendingFallbackIsCancelledByLaterIntent()
    {
        QFETCH(int, action);
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStartFailed, QStringLiteral("mic failed"));
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(fake.starts.size(), 1);
        if (action == 1) {
            test.setDevices(QStringLiteral("other-mic"), QStringLiteral("speaker"));
        } else {
            test.stop();
            if (action == 2) test.start({}, QStringLiteral("new-speaker"));
        }
        QTest::qWait(20);
        if (action == 2) {
            QCOMPARE(fake.starts.size(), 2);
            QCOMPARE(fake.starts.last().spk.deviceId, QStringLiteral("new-speaker"));
            QVERIFY(test.active());
            test.stop();
            snapshot(manager, CaptureState::Idle);
        } else {
            QCOMPARE(fake.starts.size(), 1);
            QVERIFY(!test.active());
        }
    }

    void leavingDuringAutomaticRollbackCancelsFallback()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        auto failed = channel(AudioSource::Mic, ChannelState::Failed, 0, QStringLiteral("mic failed"));
        failed.error.code = AudioErrorCode::BackendStartFailed;
        snapshot(manager, CaptureState::Stopping, {failed});
        QVERIFY(test.active());
        QVERIFY(test.stopping());
        test.stop();
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        snapshot(manager, CaptureState::Idle);
        QVERIFY(!test.stopping());
        QCOMPARE(fake.starts.size(), 1);
    }

    void unknownFailureDoesNotGuessAndDisconnectStops()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::None, AudioErrorCode::SharedMemoryFailed, QStringLiteral("queue failed"));
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(fake.starts.size(), 1);
        QVERIFY(!test.active());
        QCOMPARE(test.microphoneStatus(), QStringLiteral("queue failed"));
        QCOMPARE(test.systemStatus(), QStringLiteral("queue failed"));

        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Mic, ChannelState::Running, .7f),
                                                  channel(AudioSource::Spk, ChannelState::Running, .4f)});
        started(manager);
        issue(manager, AudioSource::Spk, AudioErrorCode::DeviceDisconnected, QStringLiteral("speaker disconnected"));
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        QCOMPARE(fake.stops.last(), QStringLiteral("capture-2"));
        QCOMPARE(test.microphoneLevel(), 0.0);
        snapshot(manager, CaptureState::Idle);
        QVERIFY(!test.stopping());
        QCOMPARE(test.systemStatus(), QStringLiteral("speaker disconnected"));
    }

    void changingDeviceStopsAndLateSignalsAreIgnored()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start({}, QStringLiteral("speaker"));
        QCOMPARE(fake.starts.size(), 1);
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Spk, ChannelState::Running, .5f)});
        started(manager);
        test.setDevices({}, QStringLiteral("other-speaker"));
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        QCOMPARE(fake.stops, QList<CaptureId>{QStringLiteral("capture-1")});
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(test.systemStatus(), QStringLiteral("设备已变更，请重新测试"));
        test.start({}, QStringLiteral("other-speaker"));
        QCOMPARE(fake.starts.size(), 2);
        CaptureSnapshot old;
        old.captureId = QStringLiteral("capture-1");
        old.state = CaptureState::Running;
        old.channels = {channel(AudioSource::Spk, ChannelState::Running, 1)};
        emit manager.captureChanged(old);
        QCOMPARE(test.systemLevel(), 0.0);
        QVERIFY(test.active());
        test.stop();
        snapshot(manager, CaptureState::Idle);
    }

    void disconnectDuringStartupStopsAndReportsStopRetry()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        auto failed = channel(AudioSource::Mic, ChannelState::Failed, 0, QStringLiteral("mic disconnected"));
        failed.error.code = AudioErrorCode::DeviceDisconnected;
        snapshot(manager, CaptureState::Stopping, {failed});
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        QCOMPARE(fake.stops, QList<CaptureId>{QStringLiteral("capture-1")});
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStopFailed, QStringLiteral("failed to stop"));
        QCOMPARE(test.microphoneStatus(), QStringLiteral("停止失败，正在重试"));
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        QCOMPARE(fake.starts.size(), 1);
        snapshot(manager, CaptureState::Idle);
        QVERIFY(!test.stopping());
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(test.microphoneStatus(), QStringLiteral("mic disconnected"));
        QCOMPARE(test.systemStatus(), QStringLiteral("设备已断开，请重新测试"));
    }

    void startupDisconnectIssueNeverStartsFallback()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Mic, AudioErrorCode::DeviceDisconnected, QStringLiteral("mic disconnected"));
        QVERIFY(!test.active());
        QVERIFY(test.stopping());
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(fake.starts.size(), 1);
        QCOMPARE(test.microphoneStatus(), QStringLiteral("mic disconnected"));
    }

    void rejectedStopDoesNotRemainStoppingForever()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start({}, QStringLiteral("speaker"));
        fake.acceptStop = false;
        test.stop();
        QVERIFY(!test.active());
        QVERIFY(!test.stopping());
        QCOMPARE(test.systemStatus(), QStringLiteral("停止失败"));
    }

    void selectingNewDeviceClearsOnlyItsOldFailure()
    {
        AudioPiplineManager manager;
        AudioLevelTest test(manager);
        test.start(QStringLiteral("mic"), QStringLiteral("speaker"));
        requestFinished(manager, PermissionState::Granted);
        issue(manager, AudioSource::Mic, AudioErrorCode::BackendStartFailed, QStringLiteral("mic failed"));
        snapshot(manager, CaptureState::Idle);
        QTRY_COMPARE(fake.starts.size(), 2);
        snapshot(manager, CaptureState::Running, {channel(AudioSource::Spk, ChannelState::Running, .5f)});
        started(manager);
        test.stop();
        snapshot(manager, CaptureState::Idle);
        QCOMPARE(test.microphoneStatus(), QStringLiteral("mic failed"));
        QCOMPARE(test.systemStatus(), QStringLiteral("已停止"));
        test.setDevices(QStringLiteral("new-mic"), QStringLiteral("speaker"));
        QCOMPARE(test.microphoneStatus(), QStringLiteral("等待测试"));
        QCOMPARE(test.systemStatus(), QStringLiteral("已停止"));
        test.setDevices({}, QStringLiteral("speaker"));
        QCOMPARE(test.microphoneStatus(), QStringLiteral("无设备"));
        QCOMPARE(test.systemStatus(), QStringLiteral("已停止"));
    }

    void busyAfterPermissionAndDestructionNeverStopsUnownedCapture()
    {
        AudioPiplineManager manager;
        {
            AudioLevelTest test(manager);
            test.start(QStringLiteral("mic"), {});
            fake.snapshot.captureId = QStringLiteral("other");
            fake.snapshot.state = CaptureState::Running;
            requestFinished(manager, PermissionState::Granted);
            QVERIFY(!test.active());
            QVERIFY(fake.starts.isEmpty());
        }
        QVERIFY(fake.stops.isEmpty());
        fake.snapshot = {};
        {
            AudioLevelTest test(manager);
            test.start({}, QStringLiteral("speaker"));
            QVERIFY(test.active());
        }
        QCOMPARE(fake.stops, QList<CaptureId>{QStringLiteral("capture-1")});
    }
};

QTEST_GUILESS_MAIN(AudioLevelTestTests)
#include "audio_level_test_tests.moc"
