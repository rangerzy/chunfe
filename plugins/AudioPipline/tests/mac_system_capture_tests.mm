// Compile this file as the only source for the test executable: it includes the
// production .mm so the actual Core Audio callback can be exercised with fake
// AudioBufferLists without starting a device or requesting permissions.
#include <QString>
#include <cstdint>
#include <functional>
#include <memory>
#include <CoreAudio/AudioHardware.h>

static OSStatus fakePropertySize(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
static OSStatus fakePropertyData(AudioObjectID, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*, void*);

#define private public
#include "../captures/mac_system_capture.h"
#undef private
#define AudioObjectGetPropertyDataSize fakePropertySize
#define AudioObjectGetPropertyData fakePropertyData
#include "../captures/mac_system_capture.mm"
#undef AudioObjectGetPropertyDataSize
#undef AudioObjectGetPropertyData

#include <cstdio>
#include <vector>

namespace {
struct FakeTopology {
    bool uidResolves = true;
    bool listed = true;
    bool alive = true;
    bool failStreamQuery = false;
    UInt32 inputStreams = 1;
    UInt32 outputStreams = 1;
} topology;
constexpr AudioObjectID fakeDevice = 123;
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

static OSStatus fakePropertyData(AudioObjectID, const AudioObjectPropertyAddress* address,
                                 UInt32, const void*, UInt32* size, void* data)
{
    switch (address->mSelector) {
    case kAudioHardwarePropertyTranslateUIDToDevice:
        *(AudioObjectID*)data = topology.uidResolves ? fakeDevice : kAudioObjectUnknown;
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
            static_cast<AudioObjectID*>(data)[i] = 500 + i;
        return noErr;
    default:
        return kAudioHardwareUnknownPropertyError;
    }
}

namespace {
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
    d->accepting.store(true);
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
    d->accepting.store(true);
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

    d->outputStream = 42;
    AudioObjectPropertyAddress changed{kAudioStreamPropertyVirtualFormat, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};
    MacSystemCapture::Impl::onProperty(42, 1, &changed, d);
    MacSystemCapture::Impl::onProperty(42, 1, &changed, d);
    if (!check(results.failures == 1 && !d->accepting.load() && d->inFlight.load() == 0,
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
