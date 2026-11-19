#include "audio_capture_context.h"

#include "cf_log.h"
#include <miniaudio.h>
#ifdef __APPLE__
#include "mac_system_capture.h"
#endif

#include <QByteArray>

using namespace AudioPipline;

namespace {
QString stableDeviceId(ma_backend backend, const ma_device_id& id)
{
    switch (backend) {
    case ma_backend_coreaudio: return QString::fromUtf8(id.coreaudio);
    case ma_backend_pulseaudio: return QString::fromUtf8(id.pulse);
    case ma_backend_alsa: return QString::fromUtf8(id.alsa);
    case ma_backend_wasapi: return QString::fromUtf16(reinterpret_cast<const char16_t*>(id.wasapi));
    default: return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(&id), sizeof(id)).toHex());
    }
}

PermissionInfo permission(AudioSource source)
{
    PermissionInfo result;
    result.source = source;
    result.state = PermissionState::Unknown;
    result.requestMode = PermissionRequestMode::OnStart;
#ifdef __APPLE__
    if (source == AudioSource::Mic) {
        result.requestMode = PermissionRequestMode::Explicit;
        result.canQueryWithoutPrompt = true;
        switch (MacSystemCapture::microphonePermission()) {
        case MacSystemCapture::Permission::Granted: result.state = PermissionState::Granted; break;
        case MacSystemCapture::Permission::Denied: result.state = PermissionState::Denied; break;
        case MacSystemCapture::Permission::Restricted: result.state = PermissionState::Restricted; break;
        case MacSystemCapture::Permission::NotDetermined: result.state = PermissionState::NotDetermined; break;
        }
    }
#endif
    return result;
}
}

struct AudioCaptureContext::Impl {
    struct Device {
        ma_device_id nativeId;
        DeviceInfo info;
    };

    ma_context context{};
    bool initialized = false;
    mutable std::mutex backendMutex;
    // 后端可能在枚举或启停时阻塞；独立快照锁让调用方仍能读取上次结果。
    mutable std::mutex snapshotMutex;
    QList<Device> devices;
    EnvironmentSnapshot environment;
};

AudioCaptureContext::AudioCaptureContext() : d(std::make_unique<Impl>())
{
    const auto status = ma_context_init(nullptr, 0, nullptr, &d->context);
    d->initialized = status == MA_SUCCESS;
    d->environment.stale = true;
    if (!d->initialized) {
        ERROR_CF_LOG().nospace() << "初始化音频上下文失败：" << status;
        d->environment.error = {AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文初始化失败")};
        return;
    }
    refresh();
}

AudioCaptureContext::~AudioCaptureContext()
{
    if (d->initialized)
        ma_context_uninit(&d->context);
}

bool AudioCaptureContext::initialized() const { return d->initialized; }

EnvironmentSnapshot AudioCaptureContext::refresh()
{
    std::lock_guard<std::mutex> backendLock(d->backendMutex);
    if (!d->initialized)
        return snapshot();

    ma_device_info *outputs = nullptr, *inputs = nullptr;
    ma_uint32 outputCount = 0, inputCount = 0;
    const auto status = ma_context_get_devices(&d->context, &outputs, &outputCount, &inputs, &inputCount);
    QList<Impl::Device> devices;
    QList<DeviceInfo> publicDevices;
    if (status == MA_SUCCESS) {
        // miniaudio 的结果由后端管理，刷新期间复制原生 ID 和对外信息。
        auto append = [&](const ma_device_info* list, ma_uint32 count, AudioSource source) {
            for (ma_uint32 i = 0; i < count; ++i) {
                DeviceInfo info;
                info.id = stableDeviceId(d->context.backend, list[i].id);
                info.name = QString::fromUtf8(list[i].name);
                info.source = source;
                info.isDefault = bool(list[i].isDefault);
                info.available = true;
#ifdef __APPLE__
                const auto direction = source == AudioSource::Mic
                    ? MacSystemCapture::DeviceDirection::Input : MacSystemCapture::DeviceDirection::Output;
                info.available = (source != AudioSource::Spk || MacSystemCapture::isSupported()) &&
                                 MacSystemCapture::isCaptureCandidate(info.id, direction);
#endif
                devices.append({list[i].id, info});
                publicDevices.append(info);
            }
        };
        append(inputs, inputCount, AudioSource::Mic);
        append(outputs, outputCount, AudioSource::Spk);
        d->devices = std::move(devices);
    } else {
        ERROR_CF_LOG().nospace() << "获取设备列表失败：" << status;
    }

    {
        std::lock_guard<std::mutex> snapshotLock(d->snapshotMutex);
        auto& environment = d->environment;
        ++environment.revision;
        environment.stale = status != MA_SUCCESS;
        if (status == MA_SUCCESS) {
            environment.devices = std::move(publicDevices);
            environment.error = {};
        } else {
            // 失败不清空上次设备，但 stale 告诉调用方不能当作当前连接状态。
            environment.error = {AudioErrorCode::DeviceUnavailable, QStringLiteral("获取设备列表失败")};
        }
    }
    return snapshot();
}

EnvironmentSnapshot AudioCaptureContext::snapshot() const
{
    EnvironmentSnapshot result;
    {
        std::lock_guard<std::mutex> lock(d->snapshotMutex);
        result = d->environment;
    }
    for (auto source : {AudioSource::Mic, AudioSource::Spk}) {
        SourceCapabilities capability;
        capability.source = source;
#ifdef __APPLE__
        capability.supported = d->initialized && (source == AudioSource::Mic || MacSystemCapture::isSupported());
#else
        capability.supported = d->initialized && (source == AudioSource::Mic || d->context.backend == ma_backend_wasapi);
#endif
        capability.supportsExplicitDeviceSelection = capability.supported;
        if (!capability.supported)
            capability.unavailableReason = QStringLiteral("当前后端不支持该音频来源");
        result.capabilities.append(capability);
        result.permissions.append(permission(source));
    }
    return result;
}

AvailabilityReport AudioCaptureContext::checkAvailability(const CaptureConfig& config) const
{
    // 只读快照：不会触发权限弹窗或打开设备，也不承诺启动时状态仍然有效。
    const auto environment = snapshot();
    AvailabilityReport result;
    result.config = config;
    for (const auto source : {AudioSource::Mic, AudioSource::Spk}) {
        if (!config.sources.testFlag(source))
            continue;
        SourceAvailability availability;
        availability.source = source;
        const auto& selection = source == AudioSource::Mic ? config.mic : config.spk;
        for (const auto& device : environment.devices) {
            if (device.source == source &&
                ((selection.mode == DeviceSelectionMode::DefaultAtStart && device.isDefault) ||
                 (selection.mode == DeviceSelectionMode::Explicit && device.id == selection.deviceId))) {
                availability.candidateDevice = device;
                break;
            }
        }
        for (const auto& permission : environment.permissions)
            if (permission.source == source) availability.permission = permission;
        bool supported = false;
        for (const auto& capability : environment.capabilities)
            if (capability.source == source) supported = capability.supported;

        if (!supported) {
            availability.status = Availability::Blocked;
            availability.error = {AudioErrorCode::UnsupportedSource, QStringLiteral("不支持该音频来源"), source};
        } else if (!availability.candidateDevice.available) {
            availability.status = Availability::Blocked;
            availability.error = {AudioErrorCode::DeviceUnavailable, QStringLiteral("设备不可用"), source};
        } else if (availability.permission.state == PermissionState::Denied ||
                   availability.permission.state == PermissionState::Restricted) {
            availability.status = Availability::Blocked;
            availability.error = {AudioErrorCode::PermissionDenied, QStringLiteral("音频权限未授权"), source};
        } else if (availability.permission.state == PermissionState::NotDetermined &&
                   availability.permission.requestMode == PermissionRequestMode::Explicit) {
            availability.status = Availability::NeedsPermission;
        } else if (availability.permission.state == PermissionState::Unknown) {
            availability.status = Availability::RequiresStartValidation;
        } else {
            availability.status = Availability::Ready;
        }
        result.sources.append(availability);
    }
    return result;
}

void AudioCaptureContext::requestPermissions(AudioSources sources, std::function<void()> completed)
{
#ifdef __APPLE__
    if (sources.testFlag(AudioSource::Mic) &&
        MacSystemCapture::microphonePermission() == MacSystemCapture::Permission::NotDetermined) {
        MacSystemCapture::requestMicrophonePermission([completed = std::move(completed)](bool) {
            if (completed) completed();
        });
        return;
    }
#else
    Q_UNUSED(sources);
#endif
    if (completed) completed();
}

std::mutex& AudioCaptureContext::backendMutex() const { return d->backendMutex; }
void* AudioCaptureContext::backendContext() const { return &d->context; }

const void* AudioCaptureContext::resolveDevice(AudioSource source, const DeviceSelection& selection,
                                               DeviceInfo& info) const
{
    for (const auto& device : d->devices) {
        if (device.info.source != source || !device.info.available)
            continue;
        if ((selection.mode == DeviceSelectionMode::DefaultAtStart && device.info.isDefault) ||
            (selection.mode == DeviceSelectionMode::Explicit && device.info.id == selection.deviceId)) {
            info = device.info;
            return &device.nativeId;
        }
    }
    return nullptr;
}
