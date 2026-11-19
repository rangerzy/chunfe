#ifndef CHUNFE_AUDIO_CAPTURE_CONTEXT_H
#define CHUNFE_AUDIO_CAPTURE_CONTEXT_H

#include "../audio_pipline_types.h"

#include <functional>
#include <memory>
#include <mutex>

class AudioCaptureWorker;

// 两个单来源 worker 共用的设备环境；原生设备 ID 和 miniaudio 上下文留在采集层。
class AudioCaptureContext
{
public:
    AudioCaptureContext();
    ~AudioCaptureContext();
    AudioCaptureContext(const AudioCaptureContext&) = delete;
    AudioCaptureContext& operator=(const AudioCaptureContext&) = delete;

    bool initialized() const;
    // 重新枚举设备并发布快照；失败时保留上次结果，但将快照标记为 stale。
    AudioPipline::EnvironmentSnapshot refresh();
    // 读取缓存及当前权限，不等待正在进行的后端枚举。
    AudioPipline::EnvironmentSnapshot snapshot() const;
    // 仅根据快照检查候选和权限；实际设备能否启动仍需后端验证。
    AudioPipline::AvailabilityReport checkAvailability(const AudioPipline::CaptureConfig& config) const;
    // 已有权限时可能同步完成；系统授权回调可能在其他线程调用 completed。
    void requestPermissions(AudioPipline::AudioSources sources, std::function<void()> completed);

private:
    friend class AudioCaptureWorker;
    struct Impl;
    std::unique_ptr<Impl> d;

    // 以下接口仅供 worker 持有 backendMutex() 时使用，原生指针不离开采集层。
    std::mutex& backendMutex() const;
    void* backendContext() const;
    // 返回缓存中的原生 ID；下一次刷新可能使指针失效，调用方须在锁内复制。
    const void* resolveDevice(AudioPipline::AudioSource source,
                              const AudioPipline::DeviceSelection& selection,
                              AudioPipline::DeviceInfo& info) const;
};

#endif
