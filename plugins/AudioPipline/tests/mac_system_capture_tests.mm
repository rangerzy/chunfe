// Compile this file as the only source for the test executable: it includes the
// production .mm so the actual Core Audio callback can be exercised with fake
// AudioBufferLists without starting a device or requesting permissions.
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <CoreAudio/AudioHardware.h>
#include <CoreAudio/AudioHardwareTapping.h>
#include <CoreAudio/CATapDescription.h>

static OSStatus fakePropertySize(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
static OSStatus fakePropertyData(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*, void*);
static OSStatus fakeAddListener(AudioObjectID, const AudioObjectPropertyAddress*, AudioObjectPropertyListenerProc, void*);
static OSStatus fakeRemoveListener(AudioObjectID, const AudioObjectPropertyAddress*, AudioObjectPropertyListenerProc, void*);
static OSStatus fakeCreateTap(CATapDescription*, AudioObjectID*);
static OSStatus fakeDestroyTap(AudioObjectID);
static OSStatus fakeCreateAggregate(CFDictionaryRef, AudioObjectID*);
static OSStatus fakeDestroyAggregate(AudioObjectID);
static OSStatus fakeCreateIoProc(AudioObjectID, AudioDeviceIOProc, void*, AudioDeviceIOProcID*);
static OSStatus fakeDestroyIoProc(AudioObjectID, AudioDeviceIOProcID);
static OSStatus fakeStart(AudioObjectID, AudioDeviceIOProcID);
static OSStatus fakeStop(AudioObjectID, AudioDeviceIOProcID);

#define private public
#include "../captures/mac_system_capture.h"
#undef private
#define AudioObjectGetPropertyDataSize fakePropertySize
#define AudioObjectGetPropertyData fakePropertyData
#define AudioObjectAddPropertyListener fakeAddListener
#define AudioObjectRemovePropertyListener fakeRemoveListener
#define AudioHardwareCreateProcessTap fakeCreateTap
#define AudioHardwareDestroyProcessTap fakeDestroyTap
#define AudioHardwareCreateAggregateDevice fakeCreateAggregate
#define AudioHardwareDestroyAggregateDevice fakeDestroyAggregate
#define AudioDeviceCreateIOProcID fakeCreateIoProc
#define AudioDeviceDestroyIOProcID fakeDestroyIoProc
#define AudioDeviceStart fakeStart
#define AudioDeviceStop fakeStop
#include "../captures/mac_system_capture.mm"
#undef AudioObjectGetPropertyDataSize
#undef AudioObjectGetPropertyData
#undef AudioObjectAddPropertyListener
#undef AudioObjectRemovePropertyListener
#undef AudioHardwareCreateProcessTap
#undef AudioHardwareDestroyProcessTap
#undef AudioHardwareCreateAggregateDevice
#undef AudioHardwareDestroyAggregateDevice
#undef AudioDeviceCreateIOProcID
#undef AudioDeviceDestroyIOProcID
#undef AudioDeviceStart
#undef AudioDeviceStop

#include <cstdio>
#include <vector>

namespace {
constexpr AudioObjectID fakeDevice = 123;
constexpr AudioObjectID fakeOutputStream = 500;
constexpr AudioObjectID fakeTap = 600;
constexpr AudioObjectID fakeAggregate = 700;

struct Listener {
    AudioObjectID object;
    AudioObjectPropertyAddress address;
    AudioObjectPropertyListenerProc callback;
    void* context;
};

struct FakeTopology {
    bool uidResolves = true;
    bool listed = true;
    bool alive = true;
    bool failStreamQuery = false;
    UInt32 inputStreams = 1;
    UInt32 outputStreams = 1;
    double physicalRate = 48000;
    double virtualRate = 48000;
    double tapRate = 48000;
    int tapsCreated = 0;
    int tapsDestroyed = 0;
    int starts = 0;
    int stops = 0;
    std::function<void()> duringStart;
    std::vector<Listener> listeners;
} topology;

AudioStreamBasicDescription stereoFloatFormat(double rate)
{
    AudioStreamBasicDescription format{};
    format.mSampleRate = rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 8 * sizeof(float);
    return format;
}

void emitProperty(AudioObjectID object, AudioObjectPropertySelector selector)
{
    const auto listeners = topology.listeners;
    for (const auto& listener : listeners) {
        if (listener.object == object && listener.address.mSelector == selector)
            listener.callback(object, 1, &listener.address, listener.context);
    }
}

void setRates(double rate)
{
    topology.physicalRate = rate;
    topology.virtualRate = rate;
    topology.tapRate = rate;
}
}

static OSStatus fakePropertySize(AudioObjectID, const AudioObjectPropertyAddress* address,
                                 UInt32, const void*, UInt32* size)
{
    if (address->mSelector == kAudioHardwarePropertyDevices) {
        *size = sizeof(AudioObjectID);
        return noErr;
    }
    if (address->mSelector == kAudioDevicePropertyStreams && !topology.failStreamQuery) {
        *size = sizeof(AudioObjectID) * (address->mScope == kAudioObjectPropertyScopeInput
            ? topology.inputStreams : topology.outputStreams);
        return noErr;
    }
    return kAudioHardwareUnknownPropertyError;
}

static OSStatus fakePropertyData(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                 UInt32 qualifierSize, const void* qualifier, UInt32* size, void* data)
{
    switch (address->mSelector) {
    case kAudioHardwarePropertyTranslateUIDToDevice:
        *(AudioObjectID*)data = topology.uidResolves && qualifier && qualifierSize == sizeof(CFStringRef) &&
                CFEqual(*static_cast<const CFStringRef*>(qualifier), CFSTR("fake-device"))
            ? fakeDevice : kAudioObjectUnknown;
        return noErr;
    case kAudioHardwarePropertyDevices:
        *(AudioObjectID*)data = topology.listed ? fakeDevice : fakeDevice + 1;
        return noErr;
    case kAudioDevicePropertyDeviceIsAlive:
        *(UInt32*)data = topology.alive ? 1 : 0;
        return noErr;
    case kAudioDevicePropertyStreams:
        if (topology.failStreamQuery) return kAudioHardwareUnknownPropertyError;
        for (UInt32 i = 0; i < *size / sizeof(AudioObjectID); ++i)
            static_cast<AudioObjectID*>(data)[i] = fakeOutputStream + i;
        return noErr;
    case kAudioDevicePropertyDeviceUID:
    case kAudioTapPropertyUID:
        *static_cast<CFStringRef*>(data) = static_cast<CFStringRef>(
            CFRetain(object == fakeDevice ? CFSTR("fake-device") : CFSTR("fake-tap")));
        return noErr;
    case kAudioStreamPropertyPhysicalFormat:
        *static_cast<AudioStreamBasicDescription*>(data) = stereoFloatFormat(topology.physicalRate);
        return noErr;
    case kAudioStreamPropertyVirtualFormat:
        *static_cast<AudioStreamBasicDescription*>(data) = stereoFloatFormat(topology.virtualRate);
        return noErr;
    case kAudioTapPropertyFormat:
        *static_cast<AudioStreamBasicDescription*>(data) = stereoFloatFormat(topology.tapRate);
        return noErr;
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

static OSStatus fakeAddListener(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                AudioObjectPropertyListenerProc callback, void* context)
{
    topology.listeners.push_back({object, *address, callback, context});
    return noErr;
}

static OSStatus fakeRemoveListener(AudioObjectID object, const AudioObjectPropertyAddress* address,
                                   AudioObjectPropertyListenerProc callback, void* context)
{
    const auto listener = std::find_if(topology.listeners.begin(), topology.listeners.end(),
        [&](const Listener& item) {
            return item.object == object && item.address.mSelector == address->mSelector &&
                   item.address.mScope == address->mScope && item.address.mElement == address->mElement &&
                   item.callback == callback && item.context == context;
        });
    if (listener == topology.listeners.end())
        return kAudioHardwareUnknownPropertyError;
    topology.listeners.erase(listener);
    return noErr;
}

static OSStatus fakeCreateTap(CATapDescription*, AudioObjectID* tap)
{
    *tap = fakeTap + topology.tapsCreated++;
    return noErr;
}

static OSStatus fakeDestroyTap(AudioObjectID)
{
    ++topology.tapsDestroyed;
    return noErr;
}

static OSStatus fakeCreateAggregate(CFDictionaryRef, AudioObjectID* aggregate)
{
    *aggregate = fakeAggregate + topology.tapsCreated;
    return noErr;
}

static OSStatus fakeDestroyAggregate(AudioObjectID)
{
    return noErr;
}

static OSStatus fakeCreateIoProc(AudioObjectID, AudioDeviceIOProc callback, void*, AudioDeviceIOProcID* ioProc)
{
    *ioProc = callback;
    return noErr;
}

static OSStatus fakeDestroyIoProc(AudioObjectID, AudioDeviceIOProcID)
{
    return noErr;
}

static OSStatus fakeStart(AudioObjectID, AudioDeviceIOProcID)
{
    ++topology.starts;
    if (topology.duringStart)
        topology.duringStart();
    return noErr;
}

static OSStatus fakeStop(AudioObjectID, AudioDeviceIOProcID)
{
    ++topology.stops;
    return noErr;
}

namespace {
using Gate = MacSystemCapture::Impl::Gate;

struct Results {
    std::vector<std::int16_t> pcm;
    std::vector<std::uint64_t> remaining;
    int failures = 0;
};

void onSamples(void* context, const std::int16_t* pcm, std::uint32_t frames,
               std::uint32_t rate, std::uint32_t channels, std::uint64_t remainingInputNs)
{
    auto& result = *static_cast<Results*>(context);
    if (rate != 16000 || channels != 1) {
        ++result.failures;
        return;
    }
    result.pcm.insert(result.pcm.end(), pcm, pcm + frames * channels);
    result.remaining.push_back(remainingInputNs);
}

void onFailure(void* context, const char*)
{
    ++static_cast<Results*>(context)->failures;
}

bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool initConverter(MacSystemCapture::Impl* d, std::uint32_t rate, std::uint32_t channels)
{
    d->rate = rate;
    d->channelCount = channels;
    auto config = ma_data_converter_config_init(ma_format_f32, ma_format_s16, channels, 1, rate, 16000);
    config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
    d->converterReady = ma_data_converter_init(&config, nullptr, &d->converter) == MA_SUCCESS;
    return d->converterReady;
}

// 通过生产 IO 回调喂入相同波形，验证跨不规则输入块的重采样状态。
std::vector<std::int16_t> runTone(unsigned rate, float frequency, bool fragmented,
                                  std::vector<std::uint64_t>* remaining = nullptr)
{
    MacSystemCapture capture;
    Results result;
    auto* d = capture.d.get();
    d->callback = onSamples;
    d->userData = &result;
    if (!initConverter(d, rate, 2)) return {};
    d->format.mBitsPerChannel = 32;
    d->format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    d->gate.store(Gate::Running);
    std::vector<float> samples(rate * 2);
    constexpr double pi = 3.14159265358979323846;
    for (std::size_t i = 0; i < rate; ++i) {
        const float v = 0.6f * std::sin(2.0 * pi * frequency * i / rate);
        samples[i * 2] = v;
        samples[i * 2 + 1] = 0.0f;
    }
    constexpr unsigned chunks[] = {1, 97, 513, 8, 321, 64, 17, 1287};
    unsigned chunkIndex = 0;
    for (std::size_t offset = 0; offset < rate;) {
        const auto count = std::min<std::size_t>(rate - offset, fragmented ? chunks[chunkIndex++ % 8] : rate);
        AudioBufferList input{};
        input.mNumberBuffers = 1;
        input.mBuffers[0] = {2, static_cast<UInt32>(count * 2 * sizeof(float)), samples.data() + offset * 2};
        MacSystemCapture::Impl::onAudio(0, nullptr, &input, nullptr, nullptr, nullptr, d);
        offset += count;
    }
    if (result.failures) return {};
    if (remaining) *remaining = result.remaining;
    return result.pcm;
}

double rms(const std::vector<std::int16_t>& data)
{
    double sum = 0;
    for (const auto value : data) sum += double(value) * value;
    return data.empty() ? 0 : std::sqrt(sum / data.size());
}

bool retriesOneSameUidFormatChange(bool duringStart)
{
    topology = {};
    Results result;
    MacSystemCapture capture;
    QString error;
    if (!check(capture.prepare(QStringLiteral("fake-device"), onSamples, &result, &error, onFailure),
               "prepare system capture with fake 48 kHz device"))
        return false;

    if (duringStart) {
        topology.duringStart = [] {
            if (topology.starts == 1) {
                setRates(44100);
                emitProperty(fakeOutputStream, kAudioStreamPropertyVirtualFormat);
            }
        };
    } else {
        setRates(44100);
    }

    bool disconnected = true;
    return check(capture.start(&error, &disconnected), "same-UID format change is recovered internally") &&
           check(error.isEmpty() && !disconnected && result.failures == 0, "recoverable startup change emits no error") &&
           check(topology.tapsCreated == 2 && topology.tapsDestroyed == 1,
                 "same-UID change retries preparation exactly once") &&
           check(topology.starts == (duringStart ? 2 : 1) && topology.stops == (duringStart ? 1 : 0),
                 "only the stable device is left running") &&
           check(capture.deviceUid() == QStringLiteral("fake-device") && capture.d->rate == 44100,
                 "converter is rebuilt for the same UID's new format") &&
           check(capture.stop() && topology.listeners.empty() && topology.tapsDestroyed == 2,
                 "cleaning up the retried capture releases both tap generations");
}

bool ignoresUnchangedFormatNotifications()
{
    topology = {};
    Results result;
    MacSystemCapture capture;
    QString error;
    if (!check(capture.prepare(QStringLiteral("fake-device"), onSamples, &result, &error, onFailure),
               "prepare fake device for format notifications"))
        return false;

    const auto unchangedNotifications = [] {
        emitProperty(fakeOutputStream, kAudioStreamPropertyPhysicalFormat);
        emitProperty(fakeOutputStream, kAudioStreamPropertyVirtualFormat);
        emitProperty(fakeOutputStream, kAudioStreamPropertyPhysicalFormat);
        emitProperty(fakeOutputStream, kAudioStreamPropertyVirtualFormat);
    };
    topology.duringStart = unchangedNotifications;
    if (!check(capture.start(&error), "duplicates during startup do not prevent start"))
        return false;
    unchangedNotifications();
    return check(error.isEmpty() && result.failures == 0 && capture.d->gate.load() == Gate::Running,
                 "duplicate unchanged formats never report a failure or close capture") &&
           check(topology.tapsCreated == 1 && topology.starts == 1, "duplicate formats do not retry") &&
           check(capture.stop(), "unchanged format device remains stoppable");
}

bool doesNotRetryDisconnectedDevice(bool duringStart)
{
    topology = {};
    Results result;
    MacSystemCapture capture;
    QString error;
    if (!check(capture.prepare(QStringLiteral("fake-device"), onSamples, &result, &error, onFailure),
               "prepare fake device before disconnect"))
        return false;

    if (duringStart) {
        topology.duringStart = [] {
            topology.listed = topology.alive = false;
            emitProperty(kAudioObjectSystemObject, kAudioHardwarePropertyDevices);
        };
    } else {
        topology.uidResolves = false;
    }

    bool disconnected = false;
    return check(!capture.start(&error, &disconnected), "disconnected selected output must not start") &&
           check(disconnected, "disconnect is reported as a typed startup outcome") &&
           check(error == QStringLiteral("系统音频设备已断开，请重新选择输出设备"),
                 "disconnected output requests selection in Chinese") &&
           check(topology.tapsCreated == 1 && topology.starts == (duringStart ? 1 : 0) && result.failures == 0,
                 "disconnect does not retry or emit an async startup failure") &&
           check(capture.stop(), "disconnected capture can be cleaned up");
}

bool confirmedDisconnectTakesPrecedenceOverFormatChange(bool notifyFormatFirst)
{
    topology = {};
    Results result;
    MacSystemCapture capture;
    QString error;
    if (!check(capture.prepare(QStringLiteral("fake-device"), onSamples, &result, &error, onFailure),
               "prepare fake device before overlapping startup changes"))
        return false;

    topology.duringStart = [notifyFormatFirst] {
        if (topology.starts != 1)
            return;
        setRates(44100);
        if (notifyFormatFirst)
            emitProperty(fakeOutputStream, kAudioStreamPropertyVirtualFormat);
        topology.uidResolves = topology.listed = false;
        emitProperty(kAudioObjectSystemObject, kAudioHardwarePropertyDevices);
        // The direct post-start check sees the same UID again, in the new format.
        topology.uidResolves = topology.listed = true;
    };
    bool disconnected = false;
    return check(!capture.start(&error, &disconnected) && disconnected &&
                     error == QStringLiteral("系统音频设备已断开，请重新选择输出设备"),
                 "confirmed disconnect takes precedence over the changed format") &&
           check(topology.starts == 1 && topology.tapsCreated == 1 && result.failures == 0,
                 "confirmed disconnect forbids retry and asynchronous startup errors") &&
           check(capture.stop(), "overlapping startup changes release the capture");
}

bool stopsAfterRepeatedFormatChanges()
{
    topology = {};
    Results result;
    MacSystemCapture capture;
    QString error;
    if (!check(capture.prepare(QStringLiteral("fake-device"), onSamples, &result, &error, onFailure),
               "prepare fake device before repeated format changes"))
        return false;

    topology.duringStart = [] {
        setRates(topology.starts == 1 ? 44100 : 32000);
        emitProperty(fakeOutputStream, kAudioStreamPropertyPhysicalFormat);
    };
    bool disconnected = true;
    return check(!capture.start(&error, &disconnected), "a second format change fails the bounded retry") &&
           check(!disconnected, "repeated format changes are not mistaken for disconnection") &&
           check(error == QStringLiteral("启动前系统音频设备仍在变化，请重试"), "retry exhaustion reports the final error") &&
           check(topology.tapsCreated == 2 && topology.starts == 2 && result.failures == 0,
                 "repeated changes never create a third tap or notify the running session") &&
           check(capture.stop(), "unstable capture can be cleaned up");
}
}

int main()
{
    using Direction = MacSystemCapture::DeviceDirection;
    const auto candidate = [](Direction direction) {
        return MacSystemCapture::isCaptureCandidate(QStringLiteral("fake-device"), direction);
    };
    if (!check(candidate(Direction::Input) && candidate(Direction::Output), "live duplex device eligible") ||
        !check(!MacSystemCapture::isCaptureCandidate({}, Direction::Input), "empty UID rejected"))
        return 1;
    topology.outputStreams = 2;
    if (!check(!candidate(Direction::Output) && candidate(Direction::Input), "multiple output streams rejected")) return 1;
    topology.outputStreams = 1;
    topology.inputStreams = 0;
    if (!check(!candidate(Direction::Input), "missing input stream rejected")) return 1;
    topology.inputStreams = 2;
    if (!check(candidate(Direction::Input), "multiple input streams allowed")) return 1;
    topology.failStreamQuery = true;
    if (!check(!candidate(Direction::Input) && !candidate(Direction::Output), "failed query rejected")) return 1;
    topology.failStreamQuery = false;
    topology.alive = false;
    if (!check(!candidate(Direction::Input) && !candidate(Direction::Output), "dead device rejected")) return 1;
    topology.alive = true;
    topology.listed = false;
    if (!check(!candidate(Direction::Input), "unlisted device rejected")) return 1;
    topology.listed = true;
    topology.uidResolves = false;
    if (!check(!candidate(Direction::Output), "unresolved UID rejected")) return 1;

    if (MacSystemCapture::isSupported() &&
        (!retriesOneSameUidFormatChange(false) || !retriesOneSameUidFormatChange(true) ||
         !ignoresUnchangedFormatNotifications() || !doesNotRetryDisconnectedDevice(false) ||
         !doesNotRetryDisconnectedDevice(true) ||
         !confirmedDisconnectTakesPrecedenceOverFormatChange(false) ||
         !confirmedDisconnectTakesPrecedenceOverFormatChange(true) ||
         !stopsAfterRepeatedFormatChanges()))
        return 1;
    topology = {};

    MacSystemCapture capture;
    QString error;
    if (!check(!capture.start(&error) && error == QStringLiteral("系统音频尚未准备"), "start requires prepare with a localized message") ||
        !check(!capture.prepare(QString(), nullptr, nullptr, &error) && error == QStringLiteral("采集回调无效或采集已准备"), "null callback has a localized message"))
        return 1;

    Results results;
    auto* d = capture.d.get();
    d->callback = onSamples;
    d->failureCallback = onFailure;
    d->userData = &results;
    if (!check(initConverter(d, 16000, 2), "initialize converter") ||
        !check(capture.sampleRate() == 16000 && capture.channels() == 1, "published format"))
        return 1;
    d->gate.store(Gate::Running);
    d->format.mBitsPerChannel = 24;
    d->format.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;

    // Two interleaved frames: minimum, maximum, zero and minus one half.
    std::uint8_t samples[] = {0, 0, 128, 255, 255, 127, 0, 0, 0, 0, 0, 192};
    AudioBufferList interleaved{};
    interleaved.mNumberBuffers = 1;
    interleaved.mBuffers[0] = {2, sizeof(samples), samples};
    MacSystemCapture::Impl::onAudio(0, nullptr, &interleaved, nullptr, nullptr, nullptr, d);
    if (!check(results.pcm.size() == 2 && std::abs(int(results.pcm[0])) < 2 &&
                   std::abs(int(results.pcm[1]) + 8192) < 2,
               "packed signed 24-bit PCM conversion and mono downmix"))
        return 1;

    results.pcm.clear();
    d->format.mBitsPerChannel = 32;
    d->format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsNonInterleaved |
                             kAudioFormatFlagIsPacked;
    float left[] = {0.25f, 0.5f};
    float right[] = {0.0f, 0.0f};
    struct TwoBuffers { UInt32 count; AudioBuffer buffers[2]; } planar{};
    planar.count = 2;
    planar.buffers[0] = {1, sizeof(left), left};
    planar.buffers[1] = {1, sizeof(right), right};
    MacSystemCapture::Impl::onAudio(0, nullptr, reinterpret_cast<AudioBufferList*>(&planar),
                                    nullptr, nullptr, nullptr, d);
    if (!check(results.pcm.size() == 2 && std::abs(int(results.pcm[0]) - 4096) < 2 &&
                   std::abs(int(results.pcm[1]) - 8192) < 2,
               "planar float PCM interleaving and mono downmix"))
        return 1;

    d->output = fakeDevice;
    d->outputStream = fakeOutputStream;
    d->physicalFormat = stereoFloatFormat(topology.physicalRate);
    d->virtualFormat = stereoFloatFormat(topology.virtualRate);
    topology.virtualRate = 44100;
    AudioObjectPropertyAddress changed{kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    MacSystemCapture::Impl::onProperty(fakeOutputStream, 1, &changed, d);
    MacSystemCapture::Impl::onProperty(fakeOutputStream, 1, &changed, d);
    if (!check(results.failures == 1 && d->gate.load() == Gate::Closed && d->inFlight.load() == 0,
               "format invalidation disables capture and reports once"))
        return 1;
    const auto oldSize = results.pcm.size();
    MacSystemCapture::Impl::onAudio(0, nullptr, &interleaved, nullptr, nullptr, nullptr, d);
    if (!check(results.pcm.size() == oldSize, "audio callback ignores data after invalidation") ||
        !check(capture.stop() && capture.stop(), "repeated stop succeeds"))
        return 1;

    for (const auto rate : {44100u, 48000u}) {
        std::vector<std::uint64_t> remaining;
        const auto whole = runTone(rate, 1000, false, &remaining);
        const auto fragmented = runTone(rate, 1000, true);
        const auto highFrequency = runTone(rate, 12000, true);
        if (!check(whole.size() >= 15998 && whole.size() <= 16002, "one second resamples to 16 kHz") ||
            !check(remaining.size() > 1 && remaining.front() > remaining.back() &&
                       std::is_sorted(remaining.rbegin(), remaining.rend()),
                   "split callbacks report decreasing remaining input time") ||
            !check(whole == fragmented, "resampling is independent of callback chunk sizes") ||
            !check(rms(whole) > 6000 && rms(whole) < 8000, "stereo downmix preserves expected amplitude") ||
            !check(rms(highFrequency) < rms(whole) * 0.1, "low-pass attenuates frequency above 8 kHz"))
            return 1;
    }

    std::puts("mac_system_capture_tests: all checks passed");
    return 0;
}
