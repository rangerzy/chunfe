#ifndef CHUNFE_AUDIO_PIPLINE_MANAGER_H
#define CHUNFE_AUDIO_PIPLINE_MANAGER_H

#include "shared_pcm_queue.h"
#include "captures/audio_capture_worker.h"

#include <QHash>
#include <QObject>
#include <QThread>
#include <chrono>
#include <memory>

class AudioCaptureContext;
class QTimer;

class AUDIO_PIPLINE_EXPORT AudioPiplineManager : public QObject
{
    Q_OBJECT
public:
    enum CaptureModel { MIC, SPK, BOTH };
    explicit AudioPiplineManager(QObject* parent = nullptr);
    ~AudioPiplineManager() override;

    // 控制方法在 manager 所在线程调用；设备查询和权限请求的结果通过 requestFinished 异步返回。
    AudioPipline::RequestId refreshDevices();
    AudioPipline::RequestId checkAvailability(const AudioPipline::CaptureConfig& config);
    AudioPipline::RequestId requestPermissions(AudioPipline::AudioSources sources);
    // 取消结果交付，不保证系统权限弹窗或正在执行的设备查询会被中止。
    bool cancelRequest(const AudioPipline::RequestId& requestId);
    // accepted 只表示受理；所有请求来源启动并确认后才会发送 captureStarted。
    AudioPipline::StartResult startCapture(const AudioPipline::CaptureConfig& config);
    AudioPipline::CommandResult pauseCapture(const AudioPipline::CaptureId& id);
    AudioPipline::CommandResult resumeCapture(const AudioPipline::CaptureId& id);
    AudioPipline::CommandResult stopCapture(const AudioPipline::CaptureId& id, AudioPipline::StopMode mode);

    AudioPipline::SharedPcmQueue& pcmQueue();
    AudioPipline::EnvironmentSnapshot environment() const;
    AudioPipline::CaptureSnapshot capture() const;
    AudioPipline::QueueInfo queueInfo() const;
    AudioPipline::QueueSnapshot queueSnapshot() const;
    void startCapture(CaptureModel mode);
    void stopCapture();
    void getDevices();

signals:
    void environmentChanged(const AudioPipline::EnvironmentSnapshot& environment);
    void requestFinished(const AudioPipline::RequestId& id, const AudioPipline::RequestResult& result);
    void captureChanged(const AudioPipline::CaptureSnapshot& capture);
    void captureStarted(const AudioPipline::CaptureId& id, const AudioPipline::CaptureInfo& info);
    // captureStopped 表示采集端已停；queueDrained 表示所需结束标记已处理、队列已排空。
    void captureStopped(const AudioPipline::CaptureId& id);
    void queueUpdated(const AudioPipline::CaptureId& id, quint64 revision);
    void queueDrained(const AudioPipline::CaptureId& id, const AudioPipline::DrainResult& result);
    void issueReported(const AudioPipline::AudioIssue& issue);

    // 命令按值跨线程发送；worker 不读取 manager 的会话状态。
    void prepareSourceRequested(AudioPipline::AudioSource source, AudioCaptureWorker::Preparation preparation);
    void startSourceRequested(AudioPipline::AudioSource source, AudioPipline::CaptureId id, quint64 operation);
    void confirmSourceRequested(AudioPipline::AudioSource source, AudioPipline::CaptureId id, quint64 operation);
    void activateSourceRequested(AudioPipline::AudioSource source, AudioPipline::CaptureId id, quint64 operation);
    void stopSourceRequested(AudioPipline::AudioSource source, AudioPipline::CaptureId id, quint64 operation);
    void refreshEnvironmentRequested(AudioPipline::RequestId id);
    void checkAvailabilityRequested(AudioPipline::RequestId id, AudioPipline::CaptureConfig config);
    void permissionsRequested(AudioPipline::RequestId id, AudioPipline::AudioSources sources);

private:
    // 每一阶段都等待对应来源的全部回执，再进入下一阶段。
    enum class StartPhase { None, Preparing, Starting, Confirming };
    enum class StopTarget { Paused, Idle };
    bool isCurrent(const AudioPipline::CaptureId& id, quint64 operation) const;
    void ensureCaptureWorkers();
    void prepareSources();
    void advanceStartup();
    void onPrepared(AudioCaptureWorker::Result result);
    void onStarted(AudioCaptureWorker::Result result);
    void onConfirmed(AudioCaptureWorker::Result result);
    void onStopped(AudioCaptureWorker::Result result);
    void onSourceFailed(AudioCaptureWorker::Result result);
    void failStartingSource(const AudioCaptureWorker::Result& result);
    void markSourceFailed(AudioPipline::AudioSource source, const AudioPipline::AudioError& error);
    void beginStop(StopTarget target, AudioPipline::StopMode mode);
    void finishStop();
    void closeDrainedQueue();
    void publishEnvironment(AudioPipline::EnvironmentSnapshot environment);
    void onEnvironmentResult(AudioPipline::RequestId id, AudioPipline::RequestResult result);

    // 两个单来源 worker 共用设备控制线程；线程结束时释放 worker QObject。
    std::shared_ptr<AudioCaptureContext> m_captureContext;
    std::shared_ptr<AudioPipline::SharedPcmQueue> m_pcmQueue;
    QThread m_captureThread;
    AudioCaptureWorker* m_micWorker = nullptr;
    AudioCaptureWorker* m_spkWorker = nullptr;
    QTimer* m_levelTimer = nullptr;

    AudioPipline::EnvironmentSnapshot m_environment;
    QHash<AudioPipline::RequestId, AudioPipline::RequestKind> m_environmentRequests;
    AudioPipline::CaptureConfig m_captureConfig;
    AudioPipline::CaptureSnapshot m_captureSnapshot;
    QList<AudioPipline::SourceCaptureInfo> m_sourceInfos;
    std::chrono::steady_clock::time_point m_captureOrigin;

    // 仅 manager 线程修改会话；operation 区分同一 captureId 下的启动、恢复和停止。
    // 与之不同，snapshot.epoch 标识暂停恢复后的音频片段，供队列和结果归属使用。
    quint64 m_operation = 0;
    StartPhase m_startPhase = StartPhase::None;
    AudioPipline::AudioSources m_startingSources;
    AudioPipline::AudioSources m_pendingStartReplies;
    // resumeSources 是恢复时仍可用的来源；pendingStops 等待整体停机回执，
    // faultStops 等待单路故障停稳后补上该来源的 StreamEnd。
    AudioPipline::AudioSources m_resumeSources;
    AudioPipline::AudioSources m_pendingStops;
    AudioPipline::AudioSources m_faultStops;
    StopTarget m_stopTarget = StopTarget::Idle;
    AudioPipline::StopMode m_stopMode = AudioPipline::StopMode::Discard;
    bool m_captureStartedEmitted = false;
    bool m_captureStoppedEmitted = false;
    bool m_queueDrainedEmitted = false;
    bool m_levelsDirty = false;
    bool m_shuttingDown = false;
};
#endif
