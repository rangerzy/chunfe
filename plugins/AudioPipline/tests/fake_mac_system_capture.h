#ifndef CHUNFE_TESTS_FAKE_MAC_SYSTEM_CAPTURE_H
#define CHUNFE_TESTS_FAKE_MAC_SYSTEM_CAPTURE_H

#include "captures/mac_system_capture.h"

#include <atomic>

namespace FakeMacSystemCapture {

// Replaces only the operating-system boundary for manager/worker tests.
struct State {
    bool inputCandidate = true;
    bool outputCandidate = true;
    bool supported = true;
    bool failPrepare = false;
    bool failStart = false;
    bool disconnectOnStart = false;
    bool emitFrameOnStart = false;
    std::atomic<int> startDelayMs{0};
    std::atomic<bool> startInProgress{false};
    bool live = false;
    int startCalls = 0;
    int stopCalls = 0;
    int failStopCount = 0;
    MacSystemCapture::Callback callback = nullptr;
    MacSystemCapture::FailureCallback failureCallback = nullptr;
    void* userData = nullptr;
};

State& state();
void reset();
void sendSamples(const std::int16_t* samples, std::uint32_t frames, std::uint64_t remainingInputNs = 0);
void sendFailure();

} // namespace FakeMacSystemCapture

#endif
