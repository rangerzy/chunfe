#include "mac_system_capture.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/AudioHardware.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <miniaudio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {
constexpr std::uint32_t outputRate = 16000;
constexpr std::uint32_t outputChannels = 1;
// A confirmed disconnect supersedes a change or an ambiguous read error.
enum class RouteCheck { Ready, ReadError, Changed, Disconnected };

void setError(QString* error, const char* operation, OSStatus status)
{
    if (error) {
        // 用户只看到可翻译的操作说明；原生错误码留在日志，不能凭错误码猜测一定是权限问题。
        *error = QCoreApplication::translate("MacSystemCapture", operation);
        qWarning().noquote() << *error << "(OSStatus" << status << ')';
    }
}

bool deviceProperty(AudioObjectID object, AudioObjectPropertySelector selector, CFStringRef* value)
{
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, value) == noErr && *value;
}

AudioObjectID findOutputDevice(const QString& uid, QString* error, OSStatus* lookupStatus = nullptr)
{
    // 设备 UID 是稳定标识；仅空 UID 才在当前时刻解析系统默认输出。
    AudioObjectPropertyAddress address{};
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMain;
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    OSStatus status;
    if (uid.isEmpty()) {
        address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device);
    } else {
        const QByteArray utf8 = uid.toUtf8();
        NSString* nsUid = [[NSString alloc] initWithBytes:utf8.constData()
                                                   length:utf8.size()
                                                 encoding:NSUTF8StringEncoding];
        if (!nsUid) {
            if (lookupStatus) *lookupStatus = paramErr;
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "输出设备标识无效"), paramErr);
            return kAudioObjectUnknown;
        }
        CFStringRef cfUid = (__bridge CFStringRef)nsUid;
        address.mSelector = kAudioHardwarePropertyTranslateUIDToDevice;
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(cfUid), &cfUid, &size, &device);
    }
    if (status == noErr && size != sizeof(device)) status = kAudioHardwareBadPropertySizeError;
    if (lookupStatus) *lookupStatus = status;
    if (status != noErr || device == kAudioObjectUnknown) {
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "找不到输出设备"), status != noErr ? status : kAudioHardwareBadObjectError);
        return kAudioObjectUnknown;
    }
    return device;
}

bool missingDevice(OSStatus status)
{
    return status == kAudioHardwareBadObjectError || status == kAudioHardwareBadDeviceError;
}

RouteCheck checkDevice(AudioObjectID device)
{
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    std::array<AudioObjectID, 1024> devices{};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr ||
        size > sizeof(devices) || size % sizeof(AudioObjectID) != 0)
        return RouteCheck::ReadError;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr ||
        size > sizeof(devices) || size % sizeof(AudioObjectID) != 0)
        return RouteCheck::ReadError;
    const auto end = devices.begin() + size / sizeof(AudioObjectID);
    if (std::find(devices.begin(), end, device) == end) return RouteCheck::Disconnected;

    address.mSelector = kAudioDevicePropertyDeviceIsAlive;
    UInt32 alive = 0;
    size = sizeof(alive);
    const auto status = AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &alive);
    if (missingDevice(status)) return RouteCheck::Disconnected;
    if (status != noErr || size != sizeof(alive)) return RouteCheck::ReadError;
    return alive ? RouteCheck::Ready : RouteCheck::Disconnected;
}

RouteCheck resolveSelectedOutput(const QString& uid, AudioObjectID* output)
{
    if (uid.isEmpty()) return RouteCheck::ReadError;
    @autoreleasepool {
        OSStatus status = noErr;
        *output = findOutputDevice(uid, nullptr, &status);
        if (*output == kAudioObjectUnknown)
            return status == noErr ? RouteCheck::Disconnected : RouteCheck::ReadError;
        const auto result = checkDevice(*output);
        if (result != RouteCheck::Disconnected) return result;

        // The same UID may acquire a new object while checking the old one.
        const auto rebound = findOutputDevice(uid, nullptr);
        if (rebound == kAudioObjectUnknown || rebound == *output || checkDevice(rebound) != RouteCheck::Ready)
            return RouteCheck::Disconnected;
        *output = rebound;
        return RouteCheck::Ready;
    }
}

RouteCheck readOutputStream(AudioObjectID device, AudioObjectID* stream)
{
    // 当前 tap 固定采集输出流 0，多输出流设备无法可靠对应，因此保守拒绝。
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr ||
        size % sizeof(AudioObjectID) != 0)
        return RouteCheck::ReadError;
    if (size != sizeof(AudioObjectID)) return RouteCheck::Changed;
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, stream) != noErr)
        return RouteCheck::ReadError;
    if (size % sizeof(AudioObjectID) != 0) return RouteCheck::ReadError;
    if (size != sizeof(AudioObjectID)) return RouteCheck::Changed;
    return *stream != kAudioObjectUnknown ? RouteCheck::Ready : RouteCheck::ReadError;
}

bool hasInputStream(AudioObjectID device)
{
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr ||
        !size || size % sizeof(AudioObjectID) != 0 || size > 1024 * sizeof(AudioObjectID))
        return false;
    std::vector<AudioObjectID> streams(size / sizeof(AudioObjectID));
    const auto capacity = size;
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, streams.data()) != noErr ||
        !size || size > capacity || size % sizeof(AudioObjectID) != 0)
        return false;
    return std::any_of(streams.begin(), streams.begin() + size / sizeof(AudioObjectID),
                       [](AudioObjectID stream) { return stream != kAudioObjectUnknown; });
}

bool sameFormat(const AudioStreamBasicDescription& a, const AudioStreamBasicDescription& b)
{
    return a.mSampleRate == b.mSampleRate && a.mFormatID == b.mFormatID &&
           a.mFormatFlags == b.mFormatFlags && a.mBytesPerPacket == b.mBytesPerPacket &&
           a.mFramesPerPacket == b.mFramesPerPacket && a.mBytesPerFrame == b.mBytesPerFrame &&
           a.mChannelsPerFrame == b.mChannelsPerFrame && a.mBitsPerChannel == b.mBitsPerChannel;
}

bool streamFormat(AudioObjectID stream, AudioObjectPropertySelector selector,
                  AudioStreamBasicDescription* format)
{
    AudioObjectPropertyAddress address{selector, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*format);
    return AudioObjectGetPropertyData(stream, &address, 0, nullptr, &size, format) == noErr &&
           size == sizeof(*format);
}

RouteCheck checkFormat(AudioObjectID object, AudioObjectPropertySelector selector,
                       const AudioStreamBasicDescription& expected)
{
    AudioStreamBasicDescription current{};
    if (!streamFormat(object, selector, &current)) return RouteCheck::ReadError;
    return sameFormat(current, expected) ? RouteCheck::Ready : RouteCheck::Changed;
}

void setStartupError(QString* error, bool* deviceDisconnected, RouteCheck check, OSStatus startStatus)
{
    switch (check) {
    case RouteCheck::Disconnected:
        if (deviceDisconnected) *deviceDisconnected = true;
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "系统音频设备已断开，请重新选择输出设备"), kAudioHardwareBadDeviceError);
        break;
    case RouteCheck::Changed:
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "启动前系统音频设备仍在变化，请重试"), kAudioHardwareBadObjectError);
        break;
    case RouteCheck::ReadError:
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法确认系统音频设备及格式"), kAudioHardwareBadObjectError);
        break;
    case RouteCheck::Ready:
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法启动系统音频采集"), startStatus);
        break;
    }
}
}

struct MacSystemCapture::Impl {
    // Startup failure states use the same increasing priority as RouteCheck.
    enum class Gate { Closed, Starting, Running, StartReadError, StartChanged, StartDisconnected };

    // tap 截取选定输出的系统声音；aggregate 把 tap 接成可注册 IOProc 的私有设备。
    AudioObjectID tap = kAudioObjectUnknown;
    AudioObjectID aggregate = kAudioObjectUnknown;
    AudioObjectID output = kAudioObjectUnknown;
    AudioObjectID outputStream = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProc = nullptr;
    AudioStreamBasicDescription format{};
    AudioStreamBasicDescription physicalFormat{};
    AudioStreamBasicDescription virtualFormat{};
    Callback callback = nullptr;
    FailureCallback failureCallback = nullptr;
    void* userData = nullptr;
    QString uid;
    QString name;
    std::uint32_t rate = 0;
    std::uint32_t channelCount = 0;
    ma_data_converter converter{};
    bool converterReady = false;
    bool started = false;
    std::atomic<Gate> gate{Gate::Closed};
    std::atomic<unsigned> inFlight{0};
    bool aliveListener = false;
    bool devicesListener = false;
    bool streamsListener = false;
    bool virtualFormatListener = false;
    bool physicalFormatListener = false;
    bool tapFormatListener = false;
    // 音频回调使用预分配缓冲区完成格式转换与交错，不在回调中分配内存。
    std::array<float, 32768> scratch{};
    std::array<std::int16_t, 1024> converted{};

    RouteCheck checkSelection() const
    {
        AudioObjectID currentOutput = kAudioObjectUnknown;
        const auto result = resolveSelectedOutput(uid, &currentOutput);
        if (result != RouteCheck::Ready) return result;
        return currentOutput == output ? RouteCheck::Ready : RouteCheck::Changed;
    }

    RouteCheck checkStream() const
    {
        AudioObjectID stream = kAudioObjectUnknown;
        const auto result = readOutputStream(output, &stream);
        if (result != RouteCheck::Ready) return result;
        return stream == outputStream ? RouteCheck::Ready : RouteCheck::Changed;
    }

    RouteCheck checkRoute() const
    {
        const auto selection = checkSelection();
        if (selection != RouteCheck::Ready) return selection;
        const auto stream = checkStream();
        if (stream == RouteCheck::Changed) return stream;
        // Direct evidence of a new format takes precedence over an unrelated read failure.
        const auto physical = checkFormat(outputStream, kAudioStreamPropertyPhysicalFormat, physicalFormat);
        const auto virtualOutput = checkFormat(outputStream, kAudioStreamPropertyVirtualFormat, virtualFormat);
        const auto tapOutput = checkFormat(tap, kAudioTapPropertyFormat, format);
        if (physical == RouteCheck::Changed || virtualOutput == RouteCheck::Changed || tapOutput == RouteCheck::Changed)
            return RouteCheck::Changed;
        if (stream == RouteCheck::ReadError || physical == RouteCheck::ReadError ||
            virtualOutput == RouteCheck::ReadError || tapOutput == RouteCheck::ReadError) {
            // Distinguish a late removal/rebind from a property read failure on a live output.
            const auto currentSelection = checkSelection();
            if (currentSelection != RouteCheck::Ready) return currentSelection;
            return RouteCheck::ReadError;
        }
        return RouteCheck::Ready;
    }

    // 持续保留重采样器状态以衔接相邻回调；输出缓冲区固定，音频线程不分配内存。
    void deliver(const float* input, std::uint32_t frames, std::uint32_t inputOffset, std::uint32_t totalFrames)
    {
        std::uint64_t offset = 0;
        while (offset < frames && gate.load() == Gate::Running) {
            ma_uint64 consumed = frames - offset;
            ma_uint64 produced = converted.size();
            const auto result = ma_data_converter_process_pcm_frames(
                &converter, input + offset * channelCount, &consumed, converted.data(), &produced);
            if (result != MA_SUCCESS || (!consumed && !produced)) {
                invalidateRoute(RouteCheck::ReadError, "System audio conversion failed");
                return;
            }
            offset += consumed;
            if (produced && gate.load() == Gate::Running) {
                // 一次 Core Audio 输入可能拆成多个输出块；按原始帧进度回推，避免各块时间重叠。
                const auto remainingFrames = std::uint64_t(totalFrames) - inputOffset - offset;
                const auto remainingNs = remainingFrames * 1000000000ULL / rate;
                callback(userData, converted.data(), static_cast<std::uint32_t>(produced),
                         outputRate, outputChannels, remainingNs);
            }
        }
    }

    struct FlightGuard {
        std::atomic<unsigned>& count;
        ~FlightGuard() { count.fetch_sub(1); }
    };

    void invalidateRoute(RouteCheck result, const char* message)
    {
        Gate startupFailure = Gate::StartReadError;
        if (result == RouteCheck::Changed) startupFailure = Gate::StartChanged;
        if (result == RouteCheck::Disconnected) startupFailure = Gate::StartDisconnected;

        auto current = gate.load();
        while (current != Gate::Closed && current < startupFailure) {
            const auto next = current == Gate::Running ? Gate::Closed : startupFailure;
            if (gate.compare_exchange_weak(current, next)) {
                // Startup owns recovery. Once published, only the first runtime failure notifies the worker.
                if (current == Gate::Running && failureCallback) failureCallback(userData, message);
                return;
            }
        }
    }

    static OSStatus onProperty(AudioObjectID object, UInt32 count,
                               const AudioObjectPropertyAddress* addresses, void* context)
    {
        auto* self = static_cast<Impl*>(context);
        self->inFlight.fetch_add(1);
        FlightGuard flight{self->inFlight};
        if (self->gate.load() == Gate::Closed) return noErr;
        // 设备消失或已选输出/tap 的流、格式变化时，原先准备的转换参数不再可靠。
        RouteCheck strongest = RouteCheck::Ready;
        const char* reason = nullptr;
        for (UInt32 i = 0; i < count; ++i) {
            const auto& address = addresses[i];
            const auto selector = address.mSelector;
            const auto scope = selector == kAudioDevicePropertyStreams
                ? kAudioObjectPropertyScopeOutput : kAudioObjectPropertyScopeGlobal;
            if (address.mScope != scope || address.mElement != kAudioObjectPropertyElementMain) continue;
            RouteCheck result = RouteCheck::Ready;
            const char* message = nullptr;
            if ((object == kAudioObjectSystemObject && selector == kAudioHardwarePropertyDevices) ||
                (object == self->output && selector == kAudioDevicePropertyDeviceIsAlive)) {
                result = self->checkSelection();
                message = "System audio output device changed";
            } else if (object == self->output && selector == kAudioDevicePropertyStreams) {
                result = self->checkStream();
                message = "System audio output streams changed";
            } else if (object == self->outputStream && (selector == kAudioStreamPropertyPhysicalFormat ||
                                                         selector == kAudioStreamPropertyVirtualFormat)) {
                const auto& expected = selector == kAudioStreamPropertyPhysicalFormat ? self->physicalFormat : self->virtualFormat;
                result = checkFormat(object, selector, expected);
                message = "System audio output format changed";
            } else if (object == self->tap && selector == kAudioTapPropertyFormat) {
                result = checkFormat(object, selector, self->format);
                message = "System audio tap format changed";
            }
            if (result > strongest) {
                strongest = result;
                reason = message;
            }
        }
        if (strongest != RouteCheck::Ready) self->invalidateRoute(strongest, reason);
        return noErr;
    }

    bool addListener(AudioObjectID object, AudioObjectPropertySelector selector,
                     AudioObjectPropertyScope scope, bool& registered, QString* error)
    {
        AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
        const OSStatus status = AudioObjectAddPropertyListener(object, &address, &Impl::onProperty, this);
        if (status != noErr) {
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法监听系统音频设备"), status);
            return false;
        }
        registered = true;
        return true;
    }

    bool removeListener(AudioObjectID object, AudioObjectPropertySelector selector,
                        AudioObjectPropertyScope scope, bool& registered, QString* error)
    {
        if (!registered)
            return true;
        AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
        const OSStatus status = AudioObjectRemovePropertyListener(object, &address, &Impl::onProperty, this);
        // Core Audio has already discarded listeners when the device object is gone.
        if (status != noErr && status != kAudioHardwareBadObjectError) {
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法移除系统音频设备监听"), status);
            return false;
        }
        registered = false;
        return true;
    }

    static OSStatus onAudio(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                            const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*, void* context)
    {
        auto* self = static_cast<Impl*>(context);
        self->inFlight.fetch_add(1);
        FlightGuard flight{self->inFlight};
        if (self->gate.load() != Gate::Running || !self->callback || !input ||
            input->mNumberBuffers == 0 || !self->channelCount)
            return noErr;

        const std::uint32_t bytesPerSample = self->format.mBitsPerChannel / 8;
        std::uint32_t frames = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t totalChannels = 0;
        for (UInt32 i = 0; i < input->mNumberBuffers; ++i) {
            const AudioBuffer& buffer = input->mBuffers[i];
            if (!buffer.mData || buffer.mNumberChannels == 0)
                return noErr;
            totalChannels += buffer.mNumberChannels;
            const auto available = buffer.mDataByteSize / (bytesPerSample * buffer.mNumberChannels);
            if (available < frames)
                frames = available;
        }
        if (totalChannels != self->channelCount || !frames)
            return noErr;

        // 已交错的 Float32 可直接交付；其余受支持格式分块转换到 scratch 后交付。
        const bool isFloat = (self->format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
        if (isFloat && self->format.mBitsPerChannel == 32 && input->mNumberBuffers == 1) {
            self->deliver(static_cast<const float*>(input->mBuffers[0].mData), frames, 0, frames);
            return noErr;
        }

        const std::uint32_t maxFrames = static_cast<std::uint32_t>(self->scratch.size() / self->channelCount);
        for (std::uint32_t offset = 0; offset < frames;) {
            const std::uint32_t count = std::min(maxFrames, frames - offset);
            std::uint32_t channelOffset = 0;
            for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
                const AudioBuffer& buffer = input->mBuffers[b];
                const auto* bytes = static_cast<const std::uint8_t*>(buffer.mData);
                for (std::uint32_t f = 0; f < count; ++f) {
                    for (UInt32 c = 0; c < buffer.mNumberChannels; ++c) {
                        const auto index = (static_cast<std::size_t>(offset + f) * buffer.mNumberChannels + c) * bytesPerSample;
                        float value;
                        if (isFloat) {
                            std::memcpy(&value, bytes + index, sizeof(value));
                        } else if (bytesPerSample == 2) {
                            std::int16_t sample;
                            std::memcpy(&sample, bytes + index, sizeof(sample));
                            value = static_cast<float>(sample) / 32768.0f;
                        } else if (bytesPerSample == 3) {
                            std::int32_t sample = static_cast<std::int32_t>(bytes[index]) |
                                (static_cast<std::int32_t>(bytes[index + 1]) << 8) |
                                (static_cast<std::int32_t>(bytes[index + 2]) << 16);
                            if ((sample & 0x800000) != 0)
                                sample -= 0x1000000;
                            value = static_cast<float>(sample) / 8388608.0f;
                        } else {
                            std::int32_t sample;
                            std::memcpy(&sample, bytes + index, sizeof(sample));
                            value = static_cast<float>(sample) / 2147483648.0f;
                        }
                        self->scratch[static_cast<std::size_t>(f) * self->channelCount + channelOffset + c] = value;
                    }
                }
                channelOffset += buffer.mNumberChannels;
            }
            self->deliver(self->scratch.data(), count, offset, frames);
            offset += count;
        }
        return noErr;
    }
};

MacSystemCapture::MacSystemCapture() : d(std::make_unique<Impl>()) {}
MacSystemCapture::~MacSystemCapture()
{
    if (!stop())
        d.release(); // Core Audio 仍可能调用已关闭的回调入口；保留上下文避免悬空访问。
}

bool MacSystemCapture::isSupported() noexcept
{
    if (@available(macOS 14.2, *))
        return true;
    return false;
}

bool MacSystemCapture::prepare(const QString& deviceUid, Callback callback, void* userData, QString* error,
                               FailureCallback failure)
{
    if (error)
        error->clear();
    if (!callback || d->tap != kAudioObjectUnknown) {
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "采集回调无效或采集已准备"), paramErr);
        return false;
    }
    if (@available(macOS 14.2, *)) {
        @autoreleasepool {
            const AudioObjectID output = findOutputDevice(deviceUid, error);
            if (output == kAudioObjectUnknown)
                return false;
            AudioObjectID outputStream = kAudioObjectUnknown;
            if (checkDevice(output) != RouteCheck::Ready || readOutputStream(output, &outputStream) != RouteCheck::Ready) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "输出设备不可用或不支持多个输出流"),
                         kAudioHardwareUnsupportedOperationError);
                return false;
            }
            d->output = output;
            d->outputStream = outputStream;
            if (!streamFormat(outputStream, kAudioStreamPropertyPhysicalFormat, &d->physicalFormat) ||
                !streamFormat(outputStream, kAudioStreamPropertyVirtualFormat, &d->virtualFormat)) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法读取系统音频输出格式"), kAudioHardwareBadObjectError);
                return false;
            }

            CFStringRef uid = nullptr;
            CFStringRef name = nullptr;
            if (!deviceProperty(output, kAudioDevicePropertyDeviceUID, &uid)) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法读取输出设备标识"), kAudioHardwareBadObjectError);
                return false;
            }
            d->uid = QString::fromUtf8([(__bridge NSString*)uid UTF8String]);
            CFRelease(uid);
            if (d->uid.isEmpty() || (!deviceUid.isEmpty() && d->uid != deviceUid)) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法确认所选输出设备标识"), kAudioHardwareBadObjectError);
                return false;
            }
            if (deviceProperty(output, kAudioObjectPropertyName, &name)) {
                d->name = QString::fromUtf8([(__bridge NSString*)name UTF8String]);
                CFRelease(name);
            }
            if (d->name.isEmpty())
                d->name = d->uid;

            const QByteArray utf8 = d->uid.toUtf8();
            NSString* selectedUid = [[NSString alloc] initWithBytes:utf8.constData()
                                                              length:utf8.size()
                                                            encoding:NSUTF8StringEncoding];
            // 私有 tap 监听所选输出流，且不改变正在播放的系统声音。
            CATapDescription* description = [[CATapDescription alloc] initExcludingProcesses:@[]
                                                                                 andDeviceUID:selectedUid
                                                                                   withStream:0];
            description.privateTap = YES;
            description.muteBehavior = CATapUnmuted;
            OSStatus status = AudioHardwareCreateProcessTap(description, &d->tap);
            if (status != noErr) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法创建系统音频采集通道"), status);
                stop();
                return false;
            }

            AudioObjectPropertyAddress tapFormat{kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal,
                                                  kAudioObjectPropertyElementMain};
            UInt32 formatSize = sizeof(d->format);
            status = AudioObjectGetPropertyData(d->tap, &tapFormat, 0, nullptr, &formatSize, &d->format);
            const bool floating = (d->format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
            const bool signedInt = (d->format.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
            const bool supportedBits = (floating && d->format.mBitsPerChannel == 32) ||
                                       (!floating && signedInt && (d->format.mBitsPerChannel == 16 ||
                                        d->format.mBitsPerChannel == 24 || d->format.mBitsPerChannel == 32));
            const UInt32 bytes = d->format.mBitsPerChannel / 8;
            const bool planar = (d->format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
            const auto expectedFrameBytes = bytes * (planar ? 1 : d->format.mChannelsPerFrame);
            if (status != noErr || d->format.mFormatID != kAudioFormatLinearPCM || !supportedBits ||
                !(d->format.mFormatFlags & kAudioFormatFlagIsPacked) ||
                (d->format.mFormatFlags & kAudioFormatFlagIsBigEndian) ||
                d->format.mBytesPerFrame != expectedFrameBytes || d->format.mChannelsPerFrame == 0 ||
                d->format.mChannelsPerFrame > 32 || !std::isfinite(d->format.mSampleRate) ||
                d->format.mSampleRate < 1 || d->format.mSampleRate > 768000 ||
                std::floor(d->format.mSampleRate) != d->format.mSampleRate) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "不支持当前系统音频格式"), status != noErr ? status : kAudioHardwareUnsupportedOperationError);
                stop();
                return false;
            }
            d->rate = static_cast<std::uint32_t>(d->format.mSampleRate);
            d->channelCount = d->format.mChannelsPerFrame;
            auto converterConfig = ma_data_converter_config_init(ma_format_f32, ma_format_s16,
                d->channelCount, outputChannels, d->rate, outputRate);
            // 降采样前低通，避免高于目标奈奎斯特频率的内容折叠到语音频段。
            converterConfig.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
            if (ma_data_converter_init(&converterConfig, nullptr, &d->converter) != MA_SUCCESS) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法初始化系统音频格式转换"), kAudioHardwareUnsupportedOperationError);
                stop();
                return false;
            }
            d->converterReady = true;

            CFStringRef tapUid = nullptr;
            if (!deviceProperty(d->tap, kAudioTapPropertyUID, &tapUid)) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法读取系统音频采集通道标识"), kAudioHardwareBadObjectError);
                stop();
                return false;
            }
            NSString* tapUidString = [(__bridge NSString*)tapUid copy];
            CFRelease(tapUid);
            // 聚合设备承载 tap，并为系统音频提供 IOProc 回调入口。
            NSDictionary* aggregateDescription = @{
                @kAudioAggregateDeviceNameKey: @"Chunfe System Audio",
                @kAudioAggregateDeviceUIDKey: [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceTapListKey: @[@{@kAudioSubTapUIDKey: tapUidString}],
            };
            status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)aggregateDescription, &d->aggregate);
            if (status != noErr) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法创建系统音频聚合设备"), status);
                stop();
                return false;
            }
            d->callback = callback;
            d->failureCallback = failure;
            d->userData = userData;
            status = AudioDeviceCreateIOProcID(d->aggregate, &Impl::onAudio, d.get(), &d->ioProc);
            if (status != noErr) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法注册系统音频回调"), status);
                stop();
                return false;
            }
            if (!d->addListener(d->output, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                d->aliveListener, error) ||
                !d->addListener(kAudioObjectSystemObject, kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                d->devicesListener, error) ||
                !d->addListener(d->output, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                d->streamsListener, error) ||
                !d->addListener(d->outputStream, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal,
                                d->virtualFormatListener, error) ||
                !d->addListener(d->outputStream, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                                d->physicalFormatListener, error) ||
                !d->addListener(d->tap, kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal,
                                d->tapFormatListener, error)) {
                stop();
                return false;
            }
            return true;
        }
    }
    setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "系统音频采集需要 macOS 14.2 或更高版本"), kAudioHardwareUnsupportedOperationError);
    return false;
}

bool MacSystemCapture::start(QString* error, bool* deviceDisconnected)
{
    if (error) error->clear();
    if (deviceDisconnected) *deviceDisconnected = false;
    if (!d->ioProc || d->aggregate == kAudioObjectUnknown) {
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "系统音频尚未准备"), paramErr);
        return false;
    }
    using Gate = Impl::Gate;
    if (d->started) {
        if (d->gate.load() == Gate::Running) return true;
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "系统音频采集尚未停止，请重试"), kAudioHardwareIllegalOperationError);
        return false;
    }

    const QString selectedUid = d->uid;
    const auto callback = d->callback;
    const auto failure = d->failureCallback;
    auto* const userData = d->userData;
    bool canRetry = !selectedUid.isEmpty();
    for (;;) {
        d->gate.store(Gate::Starting);
        auto check = d->checkRoute();
        OSStatus status = noErr;
        if (check == RouteCheck::Ready) {
            if (d->gate.load() == Gate::Starting) {
                status = AudioDeviceStart(d->aggregate, d->ioProc);
                d->started = status == noErr;
            }
            // 启动也可能促使设备改变格式。确认前不交付 PCM。
            check = d->checkRoute();
        }
        Gate expected = Gate::Starting;
        if (d->started && check == RouteCheck::Ready && d->gate.compare_exchange_strong(expected, Gate::Running))
            return true;

        const auto notification = d->gate.exchange(Gate::Closed);
        while (d->inFlight.load() != 0) std::this_thread::yield();
        if (notification == Gate::StartDisconnected) check = RouteCheck::Disconnected;
        else if (notification == Gate::StartChanged) check = std::max(check, RouteCheck::Changed);
        else if (notification == Gate::StartReadError) check = std::max(check, RouteCheck::ReadError);
        if (check != RouteCheck::Changed || !canRetry) {
            if (d->started && !stop(error)) return false;
            setStartupError(error, deviceDisconnected, check, status);
            return false;
        }

        // 启用同一蓝牙耳机的麦克风时，输出可能切到另一种格式或重新注册为新对象。
        // 只按已选 UID 重新准备一次，避免误采系统默认输出或在不稳定的路由上循环。
        canRetry = false;
        if (!stop(error)) return false;
        if (!prepare(selectedUid, callback, userData, error, failure)) {
            AudioObjectID currentOutput = kAudioObjectUnknown;
            if (resolveSelectedOutput(selectedUid, &currentOutput) == RouteCheck::Disconnected)
                setStartupError(error, deviceDisconnected, RouteCheck::Disconnected, noErr);
            return false;
        }
    }
}

bool MacSystemCapture::stop(QString* error)
{
    if (error)
        error->clear();
    // 先关闭回调入口、停止设备并等待在途回调退出，再依次释放监听器、IOProc、聚合设备和 tap。
    // 任一步失败都保留尚未释放的状态，供下次 stop 从该位置继续清理。
    d->gate.store(Impl::Gate::Closed);
    if (d->started) {
        const OSStatus status = AudioDeviceStop(d->aggregate, d->ioProc);
        while (d->inFlight.load() != 0)
            std::this_thread::yield();
        if (status != noErr) {
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法停止系统音频采集"), status);
            return false;
        }
        d->started = false;
    }
    while (d->inFlight.load() != 0)
        std::this_thread::yield();
    if (!d->removeListener(d->tap, kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal,
                           d->tapFormatListener, error) ||
        !d->removeListener(d->outputStream, kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                           d->physicalFormatListener, error) ||
        !d->removeListener(d->outputStream, kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal,
                           d->virtualFormatListener, error) ||
        !d->removeListener(d->output, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                           d->streamsListener, error) ||
        !d->removeListener(kAudioObjectSystemObject, kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                           d->devicesListener, error) ||
        !d->removeListener(d->output, kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                           d->aliveListener, error))
        return false;
    while (d->inFlight.load() != 0)
        std::this_thread::yield();
    if (d->ioProc) {
        const OSStatus status = AudioDeviceDestroyIOProcID(d->aggregate, d->ioProc);
        if (status != noErr) {
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法移除系统音频回调"), status);
            return false;
        }
        d->ioProc = nullptr;
    }
    if (d->aggregate != kAudioObjectUnknown) {
        const OSStatus status = AudioHardwareDestroyAggregateDevice(d->aggregate);
        if (status != noErr) {
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法销毁系统音频聚合设备"), status);
            return false;
        }
        d->aggregate = kAudioObjectUnknown;
    }
    if (d->tap != kAudioObjectUnknown) {
        if (@available(macOS 14.2, *)) {
            const OSStatus status = AudioHardwareDestroyProcessTap(d->tap);
            if (status != noErr) {
                setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法销毁系统音频采集通道"), status);
                return false;
            }
        }
        d->tap = kAudioObjectUnknown;
    }
    // 后端与在途回调均已退出后才能释放重采样器的状态。
    if (d->converterReady) {
        ma_data_converter_uninit(&d->converter, nullptr);
        d->converterReady = false;
    }
    d->callback = nullptr;
    d->failureCallback = nullptr;
    d->userData = nullptr;
    d->rate = 0;
    d->channelCount = 0;
    d->uid.clear();
    d->name.clear();
    d->output = kAudioObjectUnknown;
    d->outputStream = kAudioObjectUnknown;
    return true;
}

QString MacSystemCapture::deviceUid() const { return d->uid; }
QString MacSystemCapture::deviceName() const { return d->name; }
std::uint32_t MacSystemCapture::sampleRate() const { return d->converterReady ? outputRate : 0; }
std::uint32_t MacSystemCapture::channels() const { return d->converterReady ? outputChannels : 0; }

bool MacSystemCapture::isCaptureCandidate(const QString& deviceUid, DeviceDirection direction)
{
    // 这里只检查设备拓扑：输入至少一条流，系统输出当前只支持一条流。
    // 权限、实际格式及 tap 创建仍要到真正准备/启动时验证。
    if (deviceUid.isEmpty()) return false;
    @autoreleasepool {
        const auto device = findOutputDevice(deviceUid, nullptr);
        if (device == kAudioObjectUnknown || checkDevice(device) != RouteCheck::Ready) return false;
        if (direction == DeviceDirection::Input) return hasInputStream(device);
        AudioObjectID stream = kAudioObjectUnknown;
        return readOutputStream(device, &stream) == RouteCheck::Ready;
    }
}

MacSystemCapture::Permission MacSystemCapture::microphonePermission()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusNotDetermined: return Permission::NotDetermined;
    case AVAuthorizationStatusAuthorized: return Permission::Granted;
    case AVAuthorizationStatusDenied: return Permission::Denied;
    case AVAuthorizationStatusRestricted: return Permission::Restricted;
    }
    return Permission::Restricted;
}

void MacSystemCapture::requestMicrophonePermission(std::function<void(bool)> callback)
{
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
        if (callback)
            callback(granted);
    }];
}
