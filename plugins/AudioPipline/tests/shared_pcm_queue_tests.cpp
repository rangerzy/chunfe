#include "shared_pcm_queue.h"

#include <QtTest>
#include <QUuid>
#include <cstring>
#include <atomic>
#include <thread>

using namespace AudioPipline;

namespace {
const ProcessingStages allStages = ProcessingStage::Convert | ProcessingStage::Resample |
    ProcessingStage::Save | ProcessingStage::Asr;
const PcmFormat monoFloat{16000, 1, SampleType::Float32, PcmLayout::Interleaved};
CaptureId newCaptureId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
PcmInfo blockPcm(const BlockView& block)
{
    const auto& h = block.header();
    return {h.format, h.frameCount, h.validBytes, h.pcmTimestampNs};
}
}

class SharedPcmQueueTests : public QObject
{
    Q_OBJECT
private slots:
    void sequentialStagesAndFullQueue();
    void invalidInputAndStaleCapture();
    void defaultQueueCanBeCreated();
    void captureWriteGapAndReuse();
    void twoCaptureProducers();
    void disabledStagesAreBypassed();
    void enabledStageWaitsForAcknowledgement();
    void cancelledBoundaryIsNotStolen();
    void heldBlockPreventsDrain();
    void fullQueuePauseCanUpgradeToFinalStop();
    void publishedSegmentStillNeedsFinalStreamEnd();
    void resumedEpochDoesNotReceiveOldCompletion();
    void sourceFailureDoesNotFinishSibling();
    void emptyStartupRollbackCompletesAsynchronously();
    void drainUpgradeCancelsPendingBoundary();
    void cancelledBoundaryWaitsForEveryEnabledStage();
    void stageFailureIsReportedAndIncludedInDrain();
};

void SharedPcmQueueTests::defaultQueueCanBeCreated()
{
    SharedPcmQueue queue;
    const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto result = queue.create(QueueConfig{}, id);
    QVERIFY2(result.status == QueueStatus::Ok, qPrintable(result.error.message));
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::sequentialStagesAndFullQueue()
{
    SharedPcmQueue queue;
    const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto created = queue.create({1, 64, allStages}, id);
    QVERIFY2(created.status == QueueStatus::Ok, qPrintable(created.error.message));
    const PcmInfo info{{16000, 1, SampleType::Int16, PcmLayout::Interleaved}, 2, 4, 123};
    auto written = queue.tryReserve(id, AudioSource::Mic, info);
    QCOMPARE(written.status, AcquireStatus::Acquired);
    unsigned char bytes[] = {1, 2, 3, 4};
    std::memcpy(written.block->data(), bytes, sizeof(bytes));
    QCOMPARE(queue.tryReserve(id, AudioSource::Spk, info).status, AcquireStatus::Full);
    QCOMPARE(queue.commit(*written.block, info).status, QueueStatus::Ok);

    for (Stage stage : {Stage::Convert, Stage::Resample, Stage::Save}) {
        auto read = queue.tryAcquire(id, stage);
        QCOMPARE(read.status, AcquireStatus::Acquired);
        QCOMPARE(read.block->header().source, static_cast<std::uint32_t>(AudioSource::Mic));
        QCOMPARE(std::memcmp(read.block->data(), bytes, sizeof(bytes)), 0);
        if (stage == Stage::Convert) { read.block->data()[0] = 9; bytes[0] = 9; }
        QCOMPARE(queue.commit(*read.block, info).status, QueueStatus::Ok);
    }
    auto asr = queue.tryAcquire(id, Stage::Asr);
    QCOMPARE(asr.status, AcquireStatus::Acquired);
    QCOMPARE(queue.close().error.code, AudioErrorCode::Busy);
    QCOMPARE(queue.release(*asr.block).status, QueueStatus::Ok);
    QCOMPARE(queue.release(*asr.block).error.code, AudioErrorCode::StaleBlock);
    QCOMPARE(queue.snapshot().usedSlots, 0u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::invalidInputAndStaleCapture()
{
    SharedPcmQueue queue;
    const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto created = queue.create({2, 64, allStages}, id);
    QVERIFY2(created.status == QueueStatus::Ok, qPrintable(created.error.message));
    const PcmInfo info{{16000, 1, SampleType::Float32, PcmLayout::Interleaved}, 2, 8, 100};
    QCOMPARE(queue.tryReserve(QStringLiteral("old"), AudioSource::Mic, info).error.code,
             AudioErrorCode::StaleCapture);
    PcmInfo invalid = info;
    invalid.validBytes = 7;
    QCOMPARE(queue.tryReserve(id, AudioSource::Mic, invalid).error.code, AudioErrorCode::InvalidArgument);
    QCOMPARE(queue.tryAcquire(id, Stage::Resample).status, AcquireStatus::WouldBlock);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::captureWriteGapAndReuse()
{
    SharedPcmQueue queue;
    const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(queue.create({1, 64, allStages}, id).status == QueueStatus::Ok);
    const PcmInfo info{{16000, 1, SampleType::Int16, PcmLayout::Interleaved}, 2, 4, 123};
    const unsigned char bytes[] = {10, 20, 30, 40};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, info, bytes), AcquireStatus::Acquired);
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, info, bytes), AcquireStatus::Full);
    QCOMPARE(queue.writeCaptured(QStringLiteral("stale"), AudioSource::Mic, info, bytes), AcquireStatus::Error);
    for (Stage stage : {Stage::Convert, Stage::Resample, Stage::Save, Stage::Asr}) {
        auto block = queue.tryAcquire(id, stage);
        QCOMPARE(block.status, AcquireStatus::Acquired);
        QCOMPARE(std::memcmp(block.block->data(), bytes, sizeof(bytes)), 0);
        if (stage == Stage::Asr) QCOMPARE(queue.release(*block.block).status, QueueStatus::Ok);
        else QCOMPARE(queue.commit(*block.block, info).status, QueueStatus::Ok);
    }
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, info, bytes), AcquireStatus::Acquired);
    auto next = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(next.status, AcquireStatus::Acquired);
    QCOMPARE(next.block->header().sourceSequence, 2u);
    QCOMPARE(next.block->header().droppedSourceFramesBefore, 2u);
    QVERIFY(next.block->header().flags & static_cast<std::uint32_t>(BlockFlag::Gap));
    QCOMPARE(queue.snapshot().droppedSourceFrames[0], 2u);
    QCOMPARE(queue.commit(*next.block, info).status, QueueStatus::Ok);
    for (Stage stage : {Stage::Resample, Stage::Save, Stage::Asr}) {
        auto block = queue.tryAcquire(id, stage);
        QCOMPARE(block.status, AcquireStatus::Acquired);
        if (stage == Stage::Asr) QCOMPARE(queue.release(*block.block).status, QueueStatus::Ok);
        else QCOMPARE(queue.commit(*block.block, info).status, QueueStatus::Ok);
    }
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::twoCaptureProducers()
{
    SharedPcmQueue queue;
    const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(queue.create({1024, 64, allStages}, id).status == QueueStatus::Ok);
    const PcmInfo info{{16000, 1, SampleType::Int16, PcmLayout::Interleaved}, 2, 4, 123};
    const unsigned char bytes[] = {10, 20, 30, 40};
    std::atomic<unsigned> accepted{0};
    auto produce = [&](AudioSource source) {
        for (int i = 0; i < 256; ++i) {
            if (queue.writeCaptured(id, source, info, bytes) == AcquireStatus::Acquired) ++accepted;
        }
    };
    std::thread mic(produce, AudioSource::Mic), spk(produce, AudioSource::Spk);
    mic.join(); spk.join();
    auto snapshot = queue.snapshot();
    QCOMPARE(snapshot.usedSlots, accepted.load());
    QCOMPARE(accepted.load() + (snapshot.droppedSourceFrames[0] + snapshot.droppedSourceFrames[1]) / 2, 512ull);
    for (Stage stage : {Stage::Convert, Stage::Resample, Stage::Save, Stage::Asr}) {
        std::uint64_t previous[2]{};
        bool seen[2]{};
        for (unsigned i = 0; i < accepted.load(); ++i) {
            auto block = queue.tryAcquire(id, stage);
            QCOMPARE(block.status, AcquireStatus::Acquired);
            QCOMPARE(block.block->header().ticket, static_cast<std::uint64_t>(i));
            const int source = block.block->header().source == static_cast<std::uint32_t>(AudioSource::Mic) ? 0 : 1;
            if (seen[source]) QVERIFY(block.block->header().sourceSequence > previous[source]);
            seen[source] = true;
            previous[source] = block.block->header().sourceSequence;
            QCOMPARE(std::memcmp(block.block->data(), bytes, sizeof(bytes)), 0);
            if (stage == Stage::Asr) QCOMPARE(queue.release(*block.block).status, QueueStatus::Ok);
            else QCOMPARE(queue.commit(*block.block, info).status, QueueStatus::Ok);
        }
    }
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::disabledStagesAreBypassed()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, {}}, id).status, QueueStatus::Ok);
    const float sample = 0.25f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QSignalSpy updates(&queue, &SharedPcmQueue::updated);
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    QCOMPARE(updates.count(), 0); // The real-time writer does not emit Qt signals.
    QCOMPARE(queue.tryAcquire(id, Stage::Convert).error.code, AudioErrorCode::InvalidState);
    QTRY_COMPARE(queue.snapshot().usedSlots, 0u);
    QVERIFY(!updates.isEmpty());
    for (auto ticket : queue.snapshot().stageTicket) QCOMPARE(ticket, 1u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::enabledStageWaitsForAcknowledgement()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, ProcessingStage::Save}, id).status, QueueStatus::Ok);
    const float sample = 0.25f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    QTRY_COMPARE(queue.snapshot().stageTicket[1], 1u);
    QTest::qWait(30);
    QCOMPARE(queue.snapshot().usedSlots, 1u);
    QCOMPARE(queue.snapshot().stageTicket[2], 0u);
    auto save = queue.tryAcquire(id, Stage::Save);
    QCOMPARE(save.status, AcquireStatus::Acquired);
    QCOMPARE(std::memcmp(save.block->data(), &sample, sizeof(sample)), 0);
    QCOMPARE(queue.commit(*save.block, pcm).status, QueueStatus::Ok);
    QTRY_COMPARE(queue.snapshot().usedSlots, 0u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::cancelledBoundaryIsNotStolen()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    QCOMPARE(drained.count(), 0);
    QTRY_COMPARE(queue.snapshot().usedSlots, 1u);
    QTest::qWait(30);
    QCOMPARE(drained.count(), 0);
    auto boundary = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(boundary.status, AcquireStatus::Acquired);
    QCOMPARE(boundary.block->header().kind, std::uint32_t(BlockKind::StreamEnd));
    QVERIFY(boundary.block->header().flags & std::uint32_t(BlockFlag::Cancelled));
    QCOMPARE(queue.commit(*boundary.block, blockPcm(*boundary.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(qvariant_cast<DrainResult>(drained.at(0).at(1)).cancelled, true);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QTest::qWait(30);
    QCOMPARE(drained.count(), 1);
    QCOMPARE(queue.snapshot().writeTicket, 1u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::heldBlockPreventsDrain()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({1, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    const float sample = 0.5f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    auto held = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(held.status, AcquireStatus::Acquired);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    QTest::qWait(40);
    QCOMPARE(queue.snapshot().writeTicket, 1u);
    QCOMPARE(drained.count(), 0);
    QCOMPARE(queue.close().error.code, AudioErrorCode::Busy);
    QCOMPARE(std::memcmp(held.block->data(), &sample, sizeof(sample)), 0);
    QCOMPARE(queue.commit(*held.block, pcm).status, QueueStatus::Cancelled);
    QTRY_COMPARE(queue.snapshot().writeTicket, 2u);
    auto boundary = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(boundary.status, AcquireStatus::Acquired);
    QCOMPARE(queue.commit(*boundary.block, blockPcm(*boundary.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::fullQueuePauseCanUpgradeToFinalStop()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({1, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    const float sample = 0.5f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    QSignalSpy segments(&queue, &SharedPcmQueue::segmentFinished);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::SegmentEnd, StopMode::Discard).status, QueueStatus::Ok);
    QTest::qWait(20);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    auto audio = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(audio.status, AcquireStatus::Acquired);
    QCOMPARE(queue.commit(*audio.block, pcm).status, QueueStatus::Cancelled);
    QTRY_COMPARE(queue.snapshot().writeTicket, 2u);
    auto boundary = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(boundary.status, AcquireStatus::Acquired);
    QCOMPARE(boundary.block->header().kind, std::uint32_t(BlockKind::StreamEnd));
    QCOMPARE(queue.commit(*boundary.block, blockPcm(*boundary.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(segments.count(), 0);
    QTest::qWait(30);
    QCOMPARE(drained.count(), 1);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::publishedSegmentStillNeedsFinalStreamEnd()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QSignalSpy segments(&queue, &SharedPcmQueue::segmentFinished);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::SegmentEnd, StopMode::Discard).status, QueueStatus::Ok);
    QTRY_COMPARE(queue.snapshot().writeTicket, 1u);
    auto segment = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(segment.status, AcquireStatus::Acquired);
    QCOMPARE(segment.block->header().kind, std::uint32_t(BlockKind::SegmentEnd));
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QCOMPARE(queue.commit(*segment.block, blockPcm(*segment.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(queue.snapshot().writeTicket, 2u);
    auto stream = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(stream.status, AcquireStatus::Acquired);
    QCOMPARE(stream.block->header().kind, std::uint32_t(BlockKind::StreamEnd));
    QCOMPARE(stream.block->header().flags & std::uint32_t(BlockFlag::Cancelled), 0u);
    QCOMPARE(queue.commit(*stream.block, blockPcm(*stream.block)).status, QueueStatus::Ok);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(segments.count(), 0);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::resumedEpochDoesNotReceiveOldCompletion()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, {}}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QSignalSpy segments(&queue, &SharedPcmQueue::segmentFinished);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    connect(&queue, &SharedPcmQueue::segmentFinished, &queue, [&](const CaptureId&, std::uint32_t epoch) {
        QCOMPARE(epoch, 0u);
        QCOMPARE(queue.resumeEpoch(id, 1, AudioSource::Mic).status, QueueStatus::Ok);
    });
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::SegmentEnd, StopMode::Discard).status, QueueStatus::Ok);
    QTRY_COMPARE(segments.count(), 1);
    QTest::qWait(30);
    QCOMPARE(segments.count(), 1);
    QCOMPARE(queue.snapshot().epoch, 1u);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
    const auto nextId = newCaptureId();
    QCOMPARE(queue.create({2, 64, {}}, nextId).status, QueueStatus::Ok);
    QCOMPARE(queue.finish(id, {}, BlockKind::StreamEnd, StopMode::Discard).error.code, AudioErrorCode::StaleCapture);
    QTest::qWait(30);
    QCOMPARE(drained.count(), 1);
    QCOMPARE(queue.snapshot().state, QueueState::Open);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::sourceFailureDoesNotFinishSibling()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, {}}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Spk, monoFloat).status, QueueStatus::Ok);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finishSource(id, AudioSource::Mic, AudioErrorCode::DeviceDisconnected).status, QueueStatus::Ok);
    QTRY_COMPARE(queue.snapshot().writeTicket, 1u);
    QTRY_COMPARE(queue.snapshot().usedSlots, 0u);
    QCOMPARE(drained.count(), 0);
    QCOMPARE(queue.snapshot().acceptingSources, AudioSources(AudioSource::Spk));
    const float sample = 0.5f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Spk, pcm, &sample), AcquireStatus::Acquired);
    QCOMPARE(queue.finish(id, AudioSource::Mic | AudioSource::Spk, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(queue.snapshot().writeTicket, 3u); // One MIC end, SPK audio, one SPK end.
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::emptyStartupRollbackCompletesAsynchronously()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({1, 64, allStages}, id).status, QueueStatus::Ok);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, {}, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    QCOMPARE(drained.count(), 0);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(queue.snapshot().writeTicket, 0u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::drainUpgradeCancelsPendingBoundary()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({1, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    const float sample = 0.5f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QTest::qWait(20);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    auto audio = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(audio.status, AcquireStatus::Acquired);
    QCOMPARE(queue.commit(*audio.block, pcm).status, QueueStatus::Cancelled);
    QTRY_COMPARE(queue.snapshot().writeTicket, 2u);
    auto boundary = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(boundary.status, AcquireStatus::Acquired);
    QVERIFY(boundary.block->header().flags & std::uint32_t(BlockFlag::Cancelled));
    QCOMPARE(queue.commit(*boundary.block, blockPcm(*boundary.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(qvariant_cast<DrainResult>(drained.at(0).at(1)).mode, StopMode::Discard);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::cancelledBoundaryWaitsForEveryEnabledStage()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({1, 64, ProcessingStage::Convert | ProcessingStage::Save}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Discard).status, QueueStatus::Ok);
    QTRY_COMPARE(queue.snapshot().writeTicket, 1u);
    auto convert = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(convert.status, AcquireStatus::Acquired);
    QCOMPARE(queue.commit(*convert.block, blockPcm(*convert.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(queue.snapshot().stageTicket[1], 1u);
    QTest::qWait(20);
    QCOMPARE(drained.count(), 0);
    auto save = queue.tryAcquire(id, Stage::Save);
    QCOMPARE(save.status, AcquireStatus::Acquired);
    QCOMPARE(save.block->header().kind, std::uint32_t(BlockKind::StreamEnd));
    QVERIFY(save.block->header().flags & std::uint32_t(BlockFlag::Cancelled));
    QCOMPARE(queue.commit(*save.block, blockPcm(*save.block)).status, QueueStatus::Cancelled);
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

void SharedPcmQueueTests::stageFailureIsReportedAndIncludedInDrain()
{
    SharedPcmQueue queue;
    const auto id = newCaptureId();
    QCOMPARE(queue.create({2, 64, ProcessingStage::Convert}, id).status, QueueStatus::Ok);
    QCOMPARE(queue.setSourceFormat(id, AudioSource::Mic, monoFloat).status, QueueStatus::Ok);
    QSignalSpy issues(&queue, &SharedPcmQueue::issueReported);
    QSignalSpy drained(&queue, &SharedPcmQueue::drained);
    const float sample = 0.5f;
    const PcmInfo pcm{monoFloat, 1, sizeof(sample), 0};
    QCOMPARE(queue.writeCaptured(id, AudioSource::Mic, pcm, &sample), AcquireStatus::Acquired);
    auto audio = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(audio.status, AcquireStatus::Acquired);
    QCOMPARE(queue.fail(*audio.block, AudioErrorCode::ProcessingFailed).status, QueueStatus::Ok);
    QCOMPARE(issues.count(), 0);
    QCOMPARE(queue.finish(id, AudioSource::Mic, BlockKind::StreamEnd, StopMode::Drain).status, QueueStatus::Ok);
    QTRY_COMPARE(issues.count(), 1);
    const auto issue = qvariant_cast<AudioIssue>(issues.at(0).at(0));
    QCOMPARE(issue.captureId, id);
    QCOMPARE(issue.ticket, 0u);
    QCOMPARE(issue.error.source, AudioSource::Mic);
    QCOMPARE(issue.error.stage, Stage::Convert);
    auto boundary = queue.tryAcquire(id, Stage::Convert);
    QCOMPARE(boundary.status, AcquireStatus::Acquired);
    QCOMPARE(queue.commit(*boundary.block, blockPcm(*boundary.block)).status, QueueStatus::Ok);
    QTRY_COMPARE(drained.count(), 1);
    const auto result = qvariant_cast<DrainResult>(drained.at(0).at(1));
    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.issues.first().error.code, AudioErrorCode::ProcessingFailed);
    QCOMPARE(result.droppedSourceFrames[0], 1u);
    QCOMPARE(queue.close().status, QueueStatus::Ok);
}

QTEST_GUILESS_MAIN(SharedPcmQueueTests)
#include "shared_pcm_queue_tests.moc"
