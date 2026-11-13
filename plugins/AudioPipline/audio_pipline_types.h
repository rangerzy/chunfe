#ifndef CHUNFE_AUDIO_PIPLINE_TYPES_H
#define CHUNFE_AUDIO_PIPLINE_TYPES_H

#include <QByteArray>
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

enum class CaptureState { Idle, Starting, Running, Paused, Stopping };
enum class ChannelState { Disabled, Starting, Running, Paused, Failed, Stopped };
enum class Stage { None, Capture };
enum class StopMode : std::uint32_t { Drain = 0, Discard = 1 };
enum class SampleType : std::uint32_t { Unknown = 0, Int16 = 1, Int24 = 2, Int32 = 3, Float32 = 4 };
enum class PcmLayout : std::uint32_t { Unknown = 0, Interleaved = 1, Planar = 2 };

struct PcmFormat {
    std::uint32_t sampleRate;
    std::uint32_t channels;
    SampleType sampleType;
    PcmLayout layout;
};

enum class AudioErrorCode : std::uint32_t {
    None = 0,
    InvalidArgument, InvalidState, Busy, StaleCapture,
    UnsupportedSource, UnsupportedFormat, PermissionDenied,
    DeviceUnavailable, DeviceDisconnected, BackendStartFailed, BackendStopFailed
};

struct AudioError {
    AudioErrorCode code = AudioErrorCode::None;
    QString message;
    AudioSource source = AudioSource::None;
    Stage stage = Stage::None;
};

struct AudioIssue {
    CaptureId captureId;
    AudioError error;
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
    bool deliverPcm = false; // 只测电平时不分配 PCM 缓冲。
};

// 16 kHz、单声道、PCM16LE。序号按来源和采集片段递增，包含被丢弃的块。
struct CapturedAudio {
    QByteArray pcm;
    quint64 sequence = 0;
    qint64 timestampNs = 0;
    quint64 droppedFramesBefore = 0;
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

} // namespace AudioPipline

Q_DECLARE_METATYPE(AudioPipline::EnvironmentSnapshot)
Q_DECLARE_METATYPE(AudioPipline::RequestResult)
Q_DECLARE_METATYPE(AudioPipline::CaptureSnapshot)
Q_DECLARE_METATYPE(AudioPipline::CaptureInfo)
Q_DECLARE_METATYPE(AudioPipline::CapturedAudio)
Q_DECLARE_METATYPE(AudioPipline::AudioIssue)
Q_DECLARE_METATYPE(AudioPipline::AudioSource)
Q_DECLARE_METATYPE(AudioPipline::AudioSources)
Q_DECLARE_METATYPE(AudioPipline::CaptureConfig)
Q_DECLARE_METATYPE(AudioPipline::DeviceSelection)
Q_DECLARE_METATYPE(AudioPipline::SourceCaptureInfo)
Q_DECLARE_METATYPE(AudioPipline::AudioError)

#endif // CHUNFE_AUDIO_PIPLINE_TYPES_H
