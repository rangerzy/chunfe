#include "fake_mac_system_capture.h"

#include <chrono>
#include <thread>
#include <utility>

namespace FakeMacSystemCapture {

State& state()
{
    static State value;
    return value;
}

void reset()
{
    auto& value = state();
    value.inputCandidate = true;
    value.outputCandidate = true;
    value.supported = true;
    value.failPrepare = false;
    value.failStart = false;
    value.disconnectOnStart = false;
    value.emitFrameOnStart = false;
    value.startDelayMs = 0;
    value.startInProgress = false;
    value.live = false;
    value.startCalls = 0;
    value.stopCalls = 0;
    value.failStopCount = 0;
    value.callback = nullptr;
    value.failureCallback = nullptr;
    value.userData = nullptr;
}

void sendSamples(const std::int16_t* samples, std::uint32_t frames, std::uint64_t remainingInputNs)
{
    auto& value = state();
    if (value.callback) value.callback(value.userData, samples, frames, 16000, 1, remainingInputNs);
}

void sendFailure()
{
    auto& value = state();
    if (value.failureCallback) value.failureCallback(value.userData, "Injected system capture failure");
}

} // namespace FakeMacSystemCapture

struct MacSystemCapture::Impl {
    QString uid;
    bool prepared = false;
};

MacSystemCapture::MacSystemCapture() : d(std::make_unique<Impl>()) {}
MacSystemCapture::~MacSystemCapture() { stop(); }

bool MacSystemCapture::prepare(const QString& deviceUid, Callback callback, void* userData,
                              QString* error, FailureCallback failure)
{
    if (FakeMacSystemCapture::state().failPrepare) {
        if (error) *error = QStringLiteral("Injected system capture preparation failure");
        return false;
    }
    if (!callback) {
        if (error) *error = QStringLiteral("Missing test callback");
        return false;
    }
    d->uid = deviceUid.isEmpty() ? QStringLiteral("fake-speaker") : deviceUid;
    d->prepared = true;
    auto& value = FakeMacSystemCapture::state();
    value.callback = callback;
    value.failureCallback = failure;
    value.userData = userData;
    value.live = true;
    return true;
}

bool MacSystemCapture::start(QString* error, bool* deviceDisconnected)
{
    auto& value = FakeMacSystemCapture::state();
    value.startInProgress.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(value.startDelayMs.load()));
    if (value.emitFrameOnStart) {
        const std::int16_t samples[] = {8192};
        FakeMacSystemCapture::sendSamples(samples, 1);
    }
    ++value.startCalls;
    value.startInProgress.store(false, std::memory_order_release);
    if (deviceDisconnected) *deviceDisconnected = value.disconnectOnStart;
    if (value.disconnectOnStart) {
        if (error) *error = QStringLiteral("系统音频设备已断开，请重新选择输出设备");
        return false;
    }
    if (!d->prepared || value.failStart) {
        if (error) *error = QStringLiteral("Injected system capture start failure");
        return false;
    }
    return true;
}

bool MacSystemCapture::stop(QString* error)
{
    if (d->prepared) {
        auto& value = FakeMacSystemCapture::state();
        ++value.stopCalls;
        if (value.failStopCount > 0) {
            --value.failStopCount;
            if (error) *error = QStringLiteral("Injected system capture stop failure");
            return false;
        }
        value.live = false;
        value.callback = nullptr;
        value.failureCallback = nullptr;
        value.userData = nullptr;
        d->prepared = false;
    }
    return true;
}

QString MacSystemCapture::deviceUid() const { return d->uid; }
QString MacSystemCapture::deviceName() const { return QStringLiteral("Fake speaker"); }
std::uint32_t MacSystemCapture::sampleRate() const { return 16000; }
std::uint32_t MacSystemCapture::channels() const { return 1; }
bool MacSystemCapture::isSupported() noexcept { return FakeMacSystemCapture::state().supported; }
bool MacSystemCapture::isCaptureCandidate(const QString& uid, DeviceDirection direction)
{
    if (uid.isEmpty()) return false;
    const auto& state = FakeMacSystemCapture::state();
    return direction == DeviceDirection::Input ? state.inputCandidate : state.outputCandidate;
}
MacSystemCapture::Permission MacSystemCapture::microphonePermission() { return Permission::Granted; }
void MacSystemCapture::requestMicrophonePermission(std::function<void(bool)> callback) { callback(true); }
