#pragma once

#include "../audio_pipline_types.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <utility>

namespace AudioPipline {

// One native capture callback writes and one manager thread reads. Each
// capture epoch gets a fresh buffer. Storage is allocated before activation.
class AudioCaptureBuffer {
public:
    static constexpr std::uint32_t SlotCount = 128;
    static constexpr std::uint32_t FramesPerSlot = 320;

    // True means the caller owns the next wake notification.
    bool write(const std::int16_t* samples, std::uint32_t frames, std::int64_t timestampNs)
    {
        if (m_sealed.load(std::memory_order_acquire)) return false;

        bool wrote = false;
        auto write = m_write.load(std::memory_order_relaxed);
        for (std::uint32_t offset = 0; offset < frames;) {
            const auto count = std::min(FramesPerSlot, frames - offset);
            const auto sequence = m_nextSequence++;
            if (write - m_read.load(std::memory_order_acquire) == SlotCount) {
                m_droppedFrames += count;
            } else {
                auto& slot = m_slots[write % SlotCount];
                std::memcpy(slot.samples.data(), samples + offset, count * sizeof(std::int16_t));
                slot.frames = count;
                slot.sequence = sequence;
                slot.timestampNs = timestampNs + std::int64_t(offset) * 1000000000 / 16000;
                slot.droppedFramesBefore = std::exchange(m_droppedFrames, 0);
                m_write.store(++write, std::memory_order_release);
                wrote = true;
            }
            offset += count;
        }
        return wrote && !m_notificationPending.exchange(true, std::memory_order_acq_rel);
    }

    bool tryTake(CapturedAudio& packet)
    {
        const auto read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) return false;

        const auto& slot = m_slots[read % SlotCount];
        packet.pcm = QByteArray(reinterpret_cast<const char*>(slot.samples.data()),
                                qsizetype(slot.frames * sizeof(std::int16_t)));
        packet.sequence = slot.sequence;
        packet.timestampNs = slot.timestampNs;
        packet.droppedFramesBefore = slot.droppedFramesBefore;
        m_read.store(read + 1, std::memory_order_release);
        return true;
    }

    // Call after draining, or after reaching the consumer's per-turn budget.
    // If a producer published data during the drain, either its wake is
    // already pending or this call claims a replacement wake.
    bool rearmNotification()
    {
        m_notificationPending.exchange(false, std::memory_order_acq_rel);
        return m_read.load(std::memory_order_relaxed) != m_write.load(std::memory_order_acquire)
            && !m_notificationPending.exchange(true, std::memory_order_acq_rel);
    }

    // A write already in flight may still publish. Wait for the worker's
    // stop acknowledgement before reading the trailing loss counter.
    void seal() { m_sealed.store(true, std::memory_order_release); }
    std::uint64_t takeTrailingDroppedFrames() { return std::exchange(m_droppedFrames, 0); }

private:
    struct Slot {
        std::array<std::int16_t, FramesPerSlot> samples;
        std::uint32_t frames;
        std::uint64_t sequence;
        std::int64_t timestampNs;
        std::uint64_t droppedFramesBefore;
    };

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::array<Slot, SlotCount> m_slots{};
    std::atomic<std::uint32_t> m_write{0};
    std::atomic<std::uint32_t> m_read{0};
    std::atomic<bool> m_notificationPending{false};
    std::atomic<bool> m_sealed{false};
    std::uint64_t m_nextSequence = 0;
    std::uint64_t m_droppedFrames = 0;
};

} // namespace AudioPipline
