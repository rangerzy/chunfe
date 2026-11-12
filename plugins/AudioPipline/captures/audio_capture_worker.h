#ifndef AUDIOCAPTUREWORKER_H
#define AUDIOCAPTUREWORKER_H

#include "../audio_pipline_types.h"

#include <QObject>
#include <chrono>
#include <memory>

class AudioCaptureContext;
namespace AudioPipline { class SharedPcmQueue; }

// 每个 worker 只负责一个来源。设备控制在采集控制线程执行，回调在后端线程执行。
class AudioCaptureWorker : public QObject
{
    Q_OBJECT
public:
    struct Output {
        // 回调持有队列，确保后端延迟停止时不会访问已销毁的队列。
        std::shared_ptr<AudioPipline::SharedPcmQueue> queue;
        AudioPipline::CaptureId captureId;
        std::chrono::steady_clock::time_point origin;
        std::uint32_t payloadCapacity = 0;
        // 与命令的 operation 相同，用于识别旧会话的迟到回调。
        quint64 generation = 0;
    };
    struct Level { float peak = 0; float rms = 0; };
    struct Preparation {
        AudioPipline::DeviceSelection selection;
        Output output;
        bool resuming = false;
    };
    struct Result {
        AudioPipline::CaptureId captureId;
        quint64 operation = 0;
        AudioPipline::SourceCaptureInfo info;
        AudioPipline::AudioError error;
        AudioPipline::EnvironmentSnapshot environment;
    };

    AudioCaptureWorker(AudioPipline::AudioSource source,
                       std::shared_ptr<AudioCaptureContext> context,
                       QObject* parent = nullptr);
    ~AudioCaptureWorker() override;

    // 以下同步控制方法只能在设备控制线程调用；恢复时沿用上次解析的设备。
    AudioPipline::AudioError prepare(const AudioPipline::DeviceSelection& selection,
                                    const Output& output, bool resuming);
    AudioPipline::AudioError startCapture();
    AudioPipline::AudioError stopCapture();
    AudioPipline::SourceCaptureInfo info() const;

    // 这些访问器使用原子值，可跨线程调用；level 的两个读数不保证来自同一帧。
    void setAccepting(bool accepting);
    bool monitoring() const;
    Level level() const;
    std::uint64_t takeOverflowFrames();

public slots:
    // 命令按值投递给两个 worker，target 选择来源，id/operation 用来丢弃过期命令。
    // prepare 打开设备，start 启动后端但不写 PCM；confirm 复核启动状态，
    // activate 才开放写入；stop 关闭写入并在设备停稳后发 stopped，失败会重试。
    void prepareSource(AudioPipline::AudioSource target, AudioCaptureWorker::Preparation preparation);
    void startSource(AudioPipline::AudioSource target, AudioPipline::CaptureId id, quint64 operation);
    void confirmSource(AudioPipline::AudioSource target, AudioPipline::CaptureId id, quint64 operation);
    void activateSource(AudioPipline::AudioSource target, AudioPipline::CaptureId id, quint64 operation);
    void stopSource(AudioPipline::AudioSource target, AudioPipline::CaptureId id, quint64 operation);
    void shutdown();

    // 环境和权限请求由 MIC worker 统一处理，结果通过 environmentRequestFinished 返回。
    void refreshEnvironment(AudioPipline::RequestId id);
    void checkAvailability(AudioPipline::RequestId id, AudioPipline::CaptureConfig config);
    void requestPermissions(AudioPipline::RequestId id, AudioPipline::AudioSources sources);

signals:
    void failed(AudioPipline::CaptureId id, quint64 generation, AudioPipline::AudioSource source);
    void prepared(AudioCaptureWorker::Result result);
    void started(AudioCaptureWorker::Result result);
    void confirmed(AudioCaptureWorker::Result result);
    void stopped(AudioCaptureWorker::Result result);
    void sourceFailed(AudioCaptureWorker::Result result);
    void levelChanged(AudioPipline::CaptureId id, AudioPipline::AudioSource source, float peak, float rms);
    void issueReported(AudioPipline::AudioIssue issue);
    void environmentRequestFinished(AudioPipline::RequestId id, AudioPipline::RequestResult result);

private:
    void handleFailure(const AudioPipline::CaptureId& id, quint64 operation);
    void retryStop();
    void publishLevel();
    struct Impl;
    std::unique_ptr<Impl> d;
};

Q_DECLARE_METATYPE(AudioCaptureWorker::Preparation)
Q_DECLARE_METATYPE(AudioCaptureWorker::Result)

#endif // AUDIOCAPTUREWORKER_H
