#ifndef CHUNFE_AUDIO_PIPLINE_TYPES_H
#define CHUNFE_AUDIO_PIPLINE_TYPES_H

#include <QFlags>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>
#include <cstdint>

#if defined(AUDIO_PIPLINE_LIBRARY)
#define AUDIO_PIPLINE_EXPORT Q_DECL_EXPORT
#else
#define AUDIO_PIPLINE_EXPORT Q_DECL_IMPORT
#endif

namespace AudioPipline {

using CaptureId = QString;
using RequestId = QString;

enum class AudioSource : std::uint32_t { None = 0, Mic = 1, Spk = 2 };
Q_DECLARE_FLAGS(AudioSources, AudioSource)
Q_DECLARE_OPERATORS_FOR_FLAGS(AudioSources)

// 只记录已完成的阶段；是否正被处理由队列内部的 claimed 标志保护。
enum class BlockState : std::uint32_t {
    Free = 0, Captured = 1, Converted = 2, Resampled = 3, ReadyForAsr = 4
};
// Draining 表示已封口、仍在处理尾标和剩余块，不代表队列已经关闭。
enum class QueueState : std::uint32_t { Closed = 0, Open = 1, Draining = 2 };
enum class CaptureState { Idle, Starting, Running, Paused, Stopping };
enum class ChannelState { Disabled, Starting, Running, Paused, Failed, Stopped };
enum class Stage { None, Capture, Convert, Resample, Save, Asr };
// QueueConfig 用此位集合声明启用的下游阶段，未启用的阶段由队列旁路。
enum class ProcessingStage : std::uint32_t { Convert = 1, Resample = 2, Save = 4, Asr = 8 };
Q_DECLARE_FLAGS(ProcessingStages, ProcessingStage)
Q_DECLARE_OPERATORS_FOR_FLAGS(ProcessingStages)
enum class StopMode : std::uint32_t { Drain = 0, Discard = 1 };
enum class BlockKind : std::uint32_t { Audio = 0, SegmentEnd = 1, StreamEnd = 2 };
enum class BlockFlag : std::uint32_t { Gap = 1, Dropped = 2, Cancelled = 4 };
enum class SampleType : std::uint32_t { Unknown = 0, Int16 = 1, Int24 = 2, Int32 = 3, Float32 = 4 };
enum class PcmLayout : std::uint32_t { Unknown = 0, Interleaved = 1, Planar = 2 };

// 可放入共享块的固定宽度结构：小端样本，Planar 为紧密排列的连续平面。
struct PcmFormat {
    std::uint32_t sampleRate;
    std::uint32_t channels;
    SampleType sampleType;
    PcmLayout layout;
};

enum class AudioErrorCode : std::uint32_t {
    None = 0,
    InvalidArgument, InvalidState, Busy, StaleCapture, StaleBlock,
    UnsupportedSource, UnsupportedFormat, PermissionDenied,
    DeviceUnavailable, DeviceDisconnected, QueueFull, BlockTooLarge,
    ProcessingFailed, StageStalled, BackendStartFailed, BackendStopFailed,
    SharedMemoryFailed, UnsupportedProtocol
};

struct AudioError {
    AudioErrorCode code = AudioErrorCode::None;
    QString message;
    AudioSource source = AudioSource::None;
    Stage stage = Stage::None;
};

struct AudioIssue {
    CaptureId captureId;
    bool hasTicket = false;
    std::uint64_t ticket = 0;
    AudioError error;
};

struct QueueConfig {
    std::uint32_t slotCount = 128;
    std::uint32_t payloadCapacity = 64 * 1024;
    // Disabled stages are explicitly bypassed; enabled stages always acknowledge each block.
    ProcessingStages enabledStages{};
};

enum class DeviceSelectionMode { DefaultAtStart, Explicit };
struct DeviceSelection {
    DeviceSelectionMode mode = DeviceSelectionMode::DefaultAtStart;
    QString deviceId;
};

struct CaptureConfig {
    AudioSources sources; // 非空；Explicit 设备必须提供 ID。
    DeviceSelection mic;
    DeviceSelection spk;
    QueueConfig queue;
};

struct PcmInfo {
    PcmFormat format{};
    std::uint32_t frameCount = 0; // 每声道样本帧数。
    std::uint32_t validBytes = 0;
    std::int64_t timestampNs = 0; // 相对本次采集单调时钟起点。
};

struct StartResult {
    bool accepted = false;
    CaptureId captureId;
    AudioError error;
};

struct CommandResult {
    bool accepted = false;
    AudioError error;
};

enum class QueueStatus { Ok, Cancelled, Error };
struct QueueResult {
    QueueStatus status = QueueStatus::Error;
    AudioError error;
};

// 以下均为进程内值对象，不直接放入共享内存。
struct QueueInfo {
    CaptureId captureId;
    QString ipcKey;
    std::uint32_t version = 1;
    std::uint64_t mappedBytes = 0;
    std::uint32_t slotCount = 0;
    std::uint32_t payloadCapacity = 0;
};

struct QueueSnapshot {
    QueueInfo info;
    QueueState state = QueueState::Closed;
    AudioSources acceptingSources;
    StopMode stopMode = StopMode::Drain;
    std::uint32_t epoch = 0;
    std::uint32_t usedSlots = 0;
    std::uint64_t writeTicket = 0;
    std::uint64_t stageTicket[4]{};
    std::uint64_t reclaimTicket = 0;
    std::uint64_t revision = 0;
    std::uint64_t droppedSourceFrames[2]{}; // MIC、SPK，按源格式计数。
    AudioError lastError;
};

struct DeviceInfo {
    QString id; // 稳定的不透明设备标识；名称不用于定位。
    QString name;
    AudioSource source = AudioSource::None;
    bool isDefault = false;
    bool available = false; // 已通过只读候选检查；权限、格式和启动仍需验证。
};

enum class PermissionState { Unknown, NotDetermined, Granted, Denied, Restricted, NotRequired };
enum class PermissionRequestMode { Explicit, OnStart, Unavailable };
struct PermissionInfo {
    AudioSource source = AudioSource::None;
    PermissionState state = PermissionState::Unknown;
    PermissionRequestMode requestMode = PermissionRequestMode::Unavailable;
    bool canQueryWithoutPrompt = false;
};

struct SourceCapabilities {
    AudioSource source = AudioSource::None;
    bool supported = false;
    bool supportsExplicitDeviceSelection = false;
    QList<PcmFormat> captureFormats;
    QString unavailableReason;
};

struct EnvironmentSnapshot {
    std::uint64_t revision = 0;
    QList<DeviceInfo> devices;
    QList<SourceCapabilities> capabilities;
    QList<PermissionInfo> permissions;
    bool refreshing = false;
    bool stale = false; // 刷新失败时可保留上次设备，但不能当作当前连接状态。
    AudioError error;
};

enum class Availability { Ready, NeedsPermission, RequiresStartValidation, Blocked };
struct SourceAvailability {
    AudioSource source = AudioSource::None;
    Availability status = Availability::Blocked;
    DeviceInfo candidateDevice;
    PermissionInfo permission;
    AudioError error;
};

struct AvailabilityReport {
    CaptureConfig config;
    QList<SourceAvailability> sources;
};

enum class RequestKind { RefreshDevices, CheckAvailability, RequestPermissions };
enum class RequestStatus { Completed, Failed, Cancelled };
struct RequestResult {
    RequestKind kind = RequestKind::RefreshDevices;
    RequestStatus status = RequestStatus::Failed;
    // kind 决定使用哪个报告；失败通过 error 返回。
    EnvironmentSnapshot environment;
    AvailabilityReport availability;
    QList<PermissionInfo> permissions;
    AudioError error;
};

struct SourceCaptureInfo {
    AudioSource source = AudioSource::None;
    DeviceInfo device;
    PcmFormat format{};
};

struct CaptureInfo {
    CaptureId captureId;
    CaptureConfig config;
    QList<SourceCaptureInfo> sources;
    QueueInfo queue;
};

struct ChannelStatus {
    AudioSource source = AudioSource::None;
    ChannelState state = ChannelState::Disabled;
    DeviceInfo device;
    // 实际采集样本的电平统计；界面显示时可按需要将其限制到 0～1。
    float rms = 0;
    float peak = 0;
    bool hasSignal = false;
    AudioError error;
};

struct CaptureSnapshot {
    CaptureId captureId;
    CaptureState state = CaptureState::Idle;
    std::uint32_t epoch = 0;
    AudioSources requestedSources;
    AudioSources runningSources;
    bool degraded = false;
    QList<ChannelStatus> channels;
};

struct DrainResult {
    StopMode mode = StopMode::Drain;
    std::uint64_t releasedBlocks = 0;
    std::uint64_t droppedSourceFrames[2]{};
    bool cancelled = false;
    QList<AudioIssue> issues;
};

} // namespace AudioPipline

Q_DECLARE_METATYPE(AudioPipline::EnvironmentSnapshot)
Q_DECLARE_METATYPE(AudioPipline::RequestResult)
Q_DECLARE_METATYPE(AudioPipline::CaptureSnapshot)
Q_DECLARE_METATYPE(AudioPipline::CaptureInfo)
Q_DECLARE_METATYPE(AudioPipline::QueueInfo)
Q_DECLARE_METATYPE(AudioPipline::QueueSnapshot)
Q_DECLARE_METATYPE(AudioPipline::DrainResult)
Q_DECLARE_METATYPE(AudioPipline::AudioIssue)
Q_DECLARE_METATYPE(AudioPipline::AudioSource)
Q_DECLARE_METATYPE(AudioPipline::AudioSources)
Q_DECLARE_METATYPE(AudioPipline::CaptureConfig)
Q_DECLARE_METATYPE(AudioPipline::DeviceSelection)
Q_DECLARE_METATYPE(AudioPipline::SourceCaptureInfo)
Q_DECLARE_METATYPE(AudioPipline::AudioError)

#endif // CHUNFE_AUDIO_PIPLINE_TYPES_H
