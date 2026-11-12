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

AudioObjectID findOutputDevice(const QString& uid, QString* error)
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
            setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "输出设备标识无效"), paramErr);
            return kAudioObjectUnknown;
        }
        CFStringRef cfUid = (__bridge CFStringRef)nsUid;
        address.mSelector = kAudioHardwarePropertyTranslateUIDToDevice;
        status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(cfUid), &cfUid, &size, &device);
    }
    if (status != noErr || device == kAudioObjectUnknown) {
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "找不到输出设备"), status != noErr ? status : kAudioHardwareBadObjectError);
        return kAudioObjectUnknown;
    }
    return device;
}

bool deviceAlive(AudioObjectID device)
{
    AudioObjectPropertyAddress address{kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(UInt32), alive = 0;
    return AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &alive) == noErr && alive != 0;
}

bool deviceListed(AudioObjectID device)
{
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    std::array<AudioObjectID, 1024> devices{};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr ||
        size > sizeof(devices) || size % sizeof(AudioObjectID) != 0)
        return false;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.data()) != noErr ||
        size > sizeof(devices) || size % sizeof(AudioObjectID) != 0)
        return false;
    for (std::size_t i = 0; i < size / sizeof(AudioObjectID); ++i) {
        if (devices[i] == device)
            return true;
    }
    return false;
}

bool singleOutputStream(AudioObjectID device, AudioObjectID* stream)
{
    // 当前 tap 固定采集输出流 0，多输出流设备无法可靠对应，因此保守拒绝。
    AudioObjectPropertyAddress address{kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                       kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr ||
        size != sizeof(AudioObjectID))
        return false;
    return AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, stream) == noErr &&
           size == sizeof(AudioObjectID) && *stream != kAudioObjectUnknown;
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
}

struct MacSystemCapture::Impl {
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
    std::atomic<bool> accepting{false};
    std::atomic<unsigned> inFlight{0};
    std::atomic<bool> failureNotified{false};
    bool aliveListener = false;
    bool devicesListener = false;
    bool streamsListener = false;
    bool virtualFormatListener = false;
    bool physicalFormatListener = false;
    bool tapFormatListener = false;
    // 音频回调使用预分配缓冲区完成格式转换与交错，不在回调中分配内存。
    std::array<float, 32768> scratch{};
    std::array<std::int16_t, 1024> converted{};

    // 持续保留重采样器状态以衔接相邻回调；输出缓冲区固定，音频线程不分配内存。
    void deliver(const float* input, std::uint32_t frames, std::uint32_t inputOffset, std::uint32_t totalFrames)
    {
        std::uint64_t offset = 0;
        while (offset < frames && accepting.load()) {
            ma_uint64 consumed = frames - offset;
            ma_uint64 produced = converted.size();
            const auto result = ma_data_converter_process_pcm_frames(
                &converter, input + offset * channelCount, &consumed, converted.data(), &produced);
            if (result != MA_SUCCESS || (!consumed && !produced)) {
                reportFailure("System audio conversion failed");
                return;
            }
            offset += consumed;
            if (produced) {
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

    void reportFailure(const char* message)
    {
        // 并发设备通知只上报一次，先关采集入口，再由上层派发停机。
        if (!accepting.load())
            return;
        inFlight.fetch_add(1);
        FlightGuard flight{inFlight};
        if (!accepting.load() || failureNotified.exchange(true))
            return;
        accepting.store(false);
        if (failureCallback)
            failureCallback(userData, message);
    }

    static OSStatus onProperty(AudioObjectID object, UInt32 count,
                               const AudioObjectPropertyAddress* addresses, void* context)
    {
        auto* self = static_cast<Impl*>(context);
        if (!self->accepting.load())
            return noErr;
        self->inFlight.fetch_add(1);
        FlightGuard flight{self->inFlight};
        if (!self->accepting.load())
            return noErr;
        // 设备消失或已选输出/tap 的流、格式变化时，原先准备的转换参数不再可靠。
        for (UInt32 i = 0; i < count; ++i) {
            const auto selector = addresses[i].mSelector;
            if (object == kAudioObjectSystemObject && selector == kAudioHardwarePropertyDevices &&
                (!deviceListed(self->output) || !deviceAlive(self->output))) {
                self->reportFailure("System audio output device disconnected");
                break;
            }
            if (object == self->output && selector == kAudioDevicePropertyDeviceIsAlive &&
                !deviceAlive(self->output)) {
                self->reportFailure("System audio output device disconnected");
                break;
            }
            if (object == self->output && selector == kAudioDevicePropertyStreams) {
                AudioObjectID stream = kAudioObjectUnknown;
                if (!singleOutputStream(self->output, &stream) || stream != self->outputStream) {
                    self->reportFailure("System audio output streams changed");
                    break;
                }
            }
            if (object == self->outputStream && (selector == kAudioStreamPropertyPhysicalFormat ||
                                                  selector == kAudioStreamPropertyVirtualFormat)) {
                self->reportFailure("System audio output format changed");
                break;
            }
            if (object == self->tap && selector == kAudioTapPropertyFormat) {
                AudioStreamBasicDescription current{};
                UInt32 size = sizeof(current);
                AudioObjectPropertyAddress addr{kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal,
                                                kAudioObjectPropertyElementMain};
                if (AudioObjectGetPropertyData(self->tap, &addr, 0, nullptr, &size, &current) != noErr ||
                    !sameFormat(current, self->format)) {
                    self->reportFailure("System audio tap format changed");
                    break;
                }
            }
        }
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
        if (!self->accepting.load())
            return noErr;
        self->inFlight.fetch_add(1);
        FlightGuard flight{self->inFlight};
        if (!self->accepting.load() || !self->callback || !input ||
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
            if (!deviceListed(output) || !deviceAlive(output) || !singleOutputStream(output, &outputStream)) {
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

bool MacSystemCapture::start(QString* error)
{
    if (error)
        error->clear();
    if (!d->ioProc || d->aggregate == kAudioObjectUnknown) {
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "系统音频尚未准备"), paramErr);
        return false;
    }
    if (d->started)
        return true;
    d->failureNotified.store(false);
    d->accepting.store(true);
    // prepare 到 start 之间设备可能断开或切换格式，重新核对后才交给 Core Audio 启动。
    AudioObjectID currentStream = kAudioObjectUnknown;
    AudioStreamBasicDescription currentFormat{};
    AudioStreamBasicDescription currentPhysicalFormat{};
    AudioStreamBasicDescription currentVirtualFormat{};
    UInt32 formatSize = sizeof(currentFormat);
    AudioObjectPropertyAddress tapFormat{kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    const bool valid = deviceListed(d->output) && deviceAlive(d->output) && singleOutputStream(d->output, &currentStream) &&
        currentStream == d->outputStream &&
        streamFormat(d->outputStream, kAudioStreamPropertyPhysicalFormat, &currentPhysicalFormat) &&
        streamFormat(d->outputStream, kAudioStreamPropertyVirtualFormat, &currentVirtualFormat) &&
        sameFormat(currentPhysicalFormat, d->physicalFormat) &&
        sameFormat(currentVirtualFormat, d->virtualFormat) &&
        AudioObjectGetPropertyData(d->tap, &tapFormat, 0, nullptr, &formatSize, &currentFormat) == noErr &&
        sameFormat(currentFormat, d->format);
    if (!valid || !d->accepting.load()) {
        d->accepting.store(false);
        while (d->inFlight.load() != 0)
            std::this_thread::yield();
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "启动前系统音频设备已变化"), kAudioHardwareBadObjectError);
        return false;
    }
    const OSStatus status = AudioDeviceStart(d->aggregate, d->ioProc);
    if (status != noErr) {
        d->accepting.store(false);
        while (d->inFlight.load() != 0)
            std::this_thread::yield();
        setError(error, QT_TRANSLATE_NOOP("MacSystemCapture", "无法启动系统音频采集"), status);
        return false;
    }
    d->started = true;
    return true;
}

bool MacSystemCapture::stop(QString* error)
{
    if (error)
        error->clear();
    // 先关闭回调入口、停止设备并等待在途回调退出，再依次释放监听器、IOProc、聚合设备和 tap。
    // 任一步失败都保留尚未释放的状态，供下次 stop 从该位置继续清理。
    d->accepting.store(false);
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
    d->failureNotified.store(false);
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
        if (device == kAudioObjectUnknown || !deviceListed(device) || !deviceAlive(device)) return false;
        if (direction == DeviceDirection::Input) return hasInputStream(device);
        AudioObjectID stream = kAudioObjectUnknown;
        return singleOutputStream(device, &stream);
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
