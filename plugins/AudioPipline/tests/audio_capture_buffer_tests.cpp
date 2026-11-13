#include "../captures/audio_capture_buffer.h"

#include <QSemaphore>
#include <QtTest>
#include <atomic>
#include <thread>

using namespace AudioPipline;

class AudioCaptureBufferTests : public QObject
{
    Q_OBJECT

private slots:
    void fullBufferReportsGapOnTheNextAcceptedPacket()
    {
        AudioCaptureBuffer buffer;
        const std::int16_t sample = 16;
        constexpr auto capacity = AudioCaptureBuffer::SlotCount;
        for (std::uint32_t i = 0; i < capacity; ++i)
            QCOMPARE(buffer.write(&sample, 1, i * 62500LL), i == 0);
        QVERIFY(!buffer.write(&sample, 1, capacity * 62500LL));
        QVERIFY(!buffer.write(&sample, 1, (capacity + 1) * 62500LL));

        CapturedAudio packet;
        QVERIFY(buffer.tryTake(packet));
        QCOMPARE(packet.sequence, quint64(0));
        // Freeing one slot lets the producer resume; already-queued packets
        // must retain their original sequence and gap metadata.
        const auto lastTimestamp = (capacity + 2) * 62500LL;
        QVERIFY(!buffer.write(&sample, 1, lastTimestamp)); // Wake is already pending.
        for (std::uint32_t i = 1; i < capacity; ++i) {
            QVERIFY(buffer.tryTake(packet));
            QCOMPARE(packet.sequence, quint64(i));
            QCOMPARE(packet.droppedFramesBefore, quint64(0));
        }
        QVERIFY(buffer.tryTake(packet));
        QCOMPARE(packet.sequence, quint64(capacity + 2));
        QCOMPARE(packet.droppedFramesBefore, quint64(2));
        QCOMPARE(packet.timestampNs, lastTimestamp);
        QCOMPARE(packet.pcm, QByteArray(reinterpret_cast<const char*>(&sample), sizeof(sample)));
        QVERIFY(!buffer.tryTake(packet));
        QVERIFY(!buffer.rearmNotification());
        QCOMPARE(buffer.takeTrailingDroppedFrames(), std::uint64_t(0));
    }

    void sealRetainsAcceptedAudioAndReportsTrailingLoss()
    {
        AudioCaptureBuffer buffer;
        const std::int16_t sample = -16;
        for (std::uint32_t i = 0; i < AudioCaptureBuffer::SlotCount; ++i)
            buffer.write(&sample, 1, 0);
        buffer.write(&sample, 1, 0);
        buffer.seal();

        CapturedAudio packet;
        QVERIFY(buffer.tryTake(packet));
        // The write after seal has room but belongs to a closed producer.
        QVERIFY(!buffer.write(&sample, 1, 0));
        std::uint32_t consumed = 1;
        while (buffer.tryTake(packet)) ++consumed;
        QCOMPARE(consumed, AudioCaptureBuffer::SlotCount);
        QCOMPARE(buffer.takeTrailingDroppedFrames(), std::uint64_t(1));
        QCOMPARE(buffer.takeTrailingDroppedFrames(), std::uint64_t(0));
    }

    void concurrentWritesAndRearmsPreserveOrder()
    {
        AudioCaptureBuffer buffer;
        constexpr int packets = 8192;
        QSemaphore credits(64);
        QSemaphore wakes;
        std::atomic<bool> abort{false};
        std::thread producer([&] {
            for (int i = 0; i < packets; ++i) {
                credits.acquire();
                if (abort.load()) return;
                const auto sample = static_cast<std::int16_t>(i);
                if (buffer.write(&sample, 1, i * 62500LL)) wakes.release();
            }
        });

        bool ordered = true;
        int consumed = 0;
        while (consumed < packets && wakes.tryAcquire(1, 3000)) {
            CapturedAudio packet;
            // Leave work for another wake on purpose. This also exercises
            // races when the consumer drains just as the producer writes.
            for (int budget = 0; budget < 11 && buffer.tryTake(packet); ++budget) {
                const auto sample = static_cast<std::int16_t>(consumed);
                ordered &= packet.sequence == quint64(consumed) && packet.droppedFramesBefore == 0 &&
                           packet.pcm == QByteArray(reinterpret_cast<const char*>(&sample), sizeof(sample));
                ++consumed;
                credits.release();
            }
            if (buffer.rearmNotification()) wakes.release();
        }
        abort.store(true);
        credits.release();
        producer.join();
        QCOMPARE(consumed, packets);
        QVERIFY(ordered);
        QCOMPARE(buffer.takeTrailingDroppedFrames(), std::uint64_t(0));
    }
};

QTEST_GUILESS_MAIN(AudioCaptureBufferTests)
#include "audio_capture_buffer_tests.moc"
