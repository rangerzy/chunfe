#include "shared_pcm_queue.h"

#include <QPointer>
#include <QSharedMemory>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace AudioPipline {
namespace {
struct SourceStats {
    explicit SourceStats(CaptureId id) : captureId(std::move(id)) {}
    const CaptureId captureId;
    std::atomic<std::uint32_t> acceptingSources{3};
    std::atomic<std::uint64_t> sequence[2]{};
    std::atomic<std::uint64_t> pendingDroppedFrames[2]{};
    std::atomic<std::uint64_t> totalDroppedFrames[2]{};
};
struct QueueCore {
    mutable std::mutex mutex;
    // BlockView 持有 core 的 shared_ptr；外部仍持有视图时共享内存不能提前销毁。
    QSharedMemory memory;
    SharedQueueHeader* header = nullptr;
    QueueInfo info;
    std::function<void(const CaptureId&, quint64)> callback;
    std::atomic<SourceStats*> stats{nullptr};
    std::atomic<unsigned> statsReaders{0};
    std::unique_ptr<SourceStats> activeStats;
    std::vector<std::unique_ptr<SourceStats>> retiredStats;
    std::array<PcmFormat, 2> sourceFormats{};
    std::chrono::steady_clock::time_point origin;
    std::array<std::optional<BoundaryInfo>, 2> pendingBoundaries;
    AudioSources streamEndsQueued;
    AudioSources segmentEndsQueued;
    std::optional<BlockKind> finishKind;
    StopMode finishMode = StopMode::Drain;
    bool completionSent = false;
    quint64 incarnation = 0;
    quint64 notifiedRevision = std::numeric_limits<quint64>::max();
    QList<AudioIssue> issues;
    qsizetype reportedIssues = 0;
};

// 采集线程不使用 atomic_load(shared_ptr)：部分标准库实现会取得内部锁。
// 读者计数保护当前统计对象，close/create 时暂存旧对象，待后续确认没有读者再回收。
struct StatsGuard {
    explicit StatsGuard(QueueCore& c) : core(c) {
        core.statsReaders.fetch_add(1);
        stats = core.stats.load();
    }
    ~StatsGuard() { core.statsReaders.fetch_sub(1); }
    QueueCore& core;
    SourceStats* stats = nullptr;
};

void retireStats(QueueCore& core)
{
    if (core.activeStats) core.retiredStats.push_back(std::move(core.activeStats));
    if (core.statsReaders.load() == 0) core.retiredStats.clear();
}

int sourceIndex(AudioSource source)
{
    if (source == AudioSource::Mic) return 0;
    if (source == AudioSource::Spk) return 1;
    return -1;
}
int stageIndex(Stage stage)
{
    switch (stage) {
    case Stage::Convert: return 0;
    case Stage::Resample: return 1;
    case Stage::Save: return 2;
    case Stage::Asr: return 3;
    default: return -1;
    }
}
BlockState stageState(Stage stage)
{
    switch (stage) {
    case Stage::Capture: return BlockState::Free;
    case Stage::Convert: return BlockState::Captured;
    case Stage::Resample: return BlockState::Converted;
    case Stage::Save: return BlockState::Resampled;
    case Stage::Asr: return BlockState::ReadyForAsr;
    default: return BlockState::Free;
    }
}
BlockState nextState(Stage stage)
{
    switch (stage) {
    case Stage::Capture: return BlockState::Captured;
    case Stage::Convert: return BlockState::Converted;
    case Stage::Resample: return BlockState::Resampled;
    case Stage::Save: return BlockState::ReadyForAsr;
    default: return BlockState::Free;
    }
}
AudioError error(AudioErrorCode code, const QString& message = {})
{
    AudioError result;
    result.code = code;
    result.message = message;
    return result;
}
QueueResult result(QueueStatus status, AudioErrorCode code = AudioErrorCode::None, const QString& message = {})
{
    return {status, error(code, message)};
}
AcquireResult acquireError(AcquireStatus status, AudioErrorCode code = AudioErrorCode::None)
{
    AcquireResult result;
    result.status = status;
    result.error = error(code);
    return result;
}
std::size_t bytesPerSample(SampleType type)
{
    switch (type) {
    case SampleType::Int16: return 2;
    case SampleType::Int24: return 3;
    case SampleType::Int32: case SampleType::Float32: return 4;
    default: return 0;
    }
}
bool validPcm(const PcmInfo& pcm)
{
    const auto sampleBytes = bytesPerSample(pcm.format.sampleType);
    if (!sampleBytes || !pcm.format.sampleRate || !pcm.format.channels ||
        (pcm.format.layout != PcmLayout::Interleaved && pcm.format.layout != PcmLayout::Planar))
        return false;
    if (pcm.frameCount && pcm.format.channels > std::numeric_limits<std::uint32_t>::max() / pcm.frameCount / sampleBytes)
        return false;
    const std::uint64_t expected = std::uint64_t(pcm.frameCount) * pcm.format.channels * sampleBytes;
    return expected == pcm.validBytes;
}
SharedBlockHeader* slot(QueueCore& core, std::uint64_t ticket)
{
    auto* base = static_cast<std::uint8_t*>(core.memory.data());
    return reinterpret_cast<SharedBlockHeader*>(base + core.header->headerBytes +
                                                (ticket % core.header->slotCount) * core.header->slotStride);
}
std::uint8_t* payload(QueueCore& core, SharedBlockHeader* block)
{
    return reinterpret_cast<std::uint8_t*>(block) + core.header->payloadOffset;
}
using Callback = std::function<void(const CaptureId&, quint64)>;
void notify(const Callback& callback, const CaptureId& id, quint64 revision)
{
    if (callback) callback(id, revision);
}
bool idMatches(const QueueCore& core, const CaptureId& id)
{
    return core.header && core.info.captureId == id;
}
void reclaim(QueueCore& core)
{
    // 环形空间只能复用从 reclaimTicket 开始的连续空闲前缀，不能跳过仍在使用的槽。
    auto& h = *core.header;
    while (h.reclaimTicket < h.writeTicket) {
        auto* block = slot(core, h.reclaimTicket);
        if (block->ticket != h.reclaimTicket || block->state != static_cast<std::uint32_t>(BlockState::Free) || block->claimed)
            break;
        ++h.reclaimTicket;
    }
}
void finishBlock(QueueCore& core, SharedBlockHeader& block, Stage stage, bool dropped, AudioErrorCode reason)
{
    // 无论成功、取消还是失败都沿原 ticket 交接；保留 kind，确保下游仍能处理尾标。
    if (reason != AudioErrorCode::None) {
        AudioIssue issue;
        issue.captureId = core.info.captureId;
        issue.hasTicket = true;
        issue.ticket = block.ticket;
        issue.error = {reason, {}, static_cast<AudioSource>(block.source), stage};
        core.issues.append(issue);
    }
    if (dropped) {
        if (!(block.flags & static_cast<std::uint32_t>(BlockFlag::Dropped)) &&
            reason != AudioErrorCode::None && block.kind == static_cast<std::uint32_t>(BlockKind::Audio)) {
            const int index = sourceIndex(static_cast<AudioSource>(block.source));
            if (auto* stats = core.stats.load(); stats && index >= 0)
                stats->totalDroppedFrames[index] += block.sourceFrameCount;
        }
        block.flags |= static_cast<std::uint32_t>(BlockFlag::Dropped);
        if (reason != AudioErrorCode::None)
            block.errorCode = static_cast<std::uint32_t>(reason);
        block.validBytes = 0;
        block.frameCount = 0;
    }
    block.state = static_cast<std::uint32_t>(nextState(stage));
    block.claimed = 0;
    const int index = stageIndex(stage);
    if (index >= 0) ++core.header->stageTicket[index];
    if (stage == Stage::Asr) reclaim(core);
    ++core.header->revision;
}

void sealLocked(QueueCore& core, AudioSources sources, StopMode mode)
{
    auto& h = *core.header;
    const auto mask = static_cast<std::uint32_t>(sources.toInt());
    h.acceptingSources &= ~mask;
    if (const auto stats = core.stats.load()) stats->acceptingSources = h.acceptingSources;
    h.stopMode = static_cast<std::uint32_t>(mode);
    if (!h.acceptingSources) h.state = static_cast<std::uint32_t>(QueueState::Draining);
    if (mode == StopMode::Discard) {
        // 只加取消标记，不修改持有者正在访问的 PCM；持有者归还后才继续推进。
        for (auto ticket = h.reclaimTicket; ticket < h.writeTicket; ++ticket) {
            auto* block = slot(core, ticket);
            if (block->ticket == ticket && (block->source & mask))
                block->flags |= static_cast<std::uint32_t>(BlockFlag::Cancelled);
        }
        for (auto& pending : core.pendingBoundaries)
            if (pending && sources.testFlag(pending->source)) pending->cancelled = true;
    }
    ++h.revision;
}

void queueBoundary(QueueCore& core, AudioSource source, BlockKind kind, StopMode mode,
                   AudioErrorCode reason = AudioErrorCode::None)
{
    // 每个来源最多一个待入队尾标：暂停升级为最终停止时，未入队的片段尾标可替换为流尾标。
    // 已入队的片段尾标则保留，后续再追加流尾标；流尾标不可重复发送。
    if (core.streamEndsQueued.testFlag(source) ||
        (kind == BlockKind::SegmentEnd && core.segmentEndsQueued.testFlag(source))) return;
    auto& pending = core.pendingBoundaries[sourceIndex(source)];
    if (pending && pending->kind == BlockKind::StreamEnd) {
        pending->cancelled |= mode == StopMode::Discard;
        return;
    }
    BoundaryInfo boundary;
    boundary.captureId = core.info.captureId;
    boundary.source = source;
    boundary.kind = kind;
    boundary.epoch = core.header->epoch;
    boundary.cancelled = mode == StopMode::Discard;
    boundary.error = reason;
    boundary.inputFormat = core.sourceFormats[sourceIndex(source)];
    boundary.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - core.origin).count();
    pending = boundary;
}

bool hasPendingBoundaries(const QueueCore& core)
{
    return std::any_of(core.pendingBoundaries.begin(), core.pendingBoundaries.end(),
                       [](const auto& pending) { return pending.has_value(); });
}

bool bypassDisabledStages(QueueCore& core, unsigned& budget)
{
    // 只代为推进未启用的阶段，且遵守阶段游标和 claimed；启用阶段必须自行确认。
    bool progressed = false;
    auto& h = *core.header;
    for (Stage stage : {Stage::Convert, Stage::Resample, Stage::Save, Stage::Asr}) {
        const int index = stageIndex(stage);
        if (h.reserved[0] & (1u << index)) continue;
        while (budget && h.stageTicket[index] < h.writeTicket) {
            const auto ticket = h.stageTicket[index];
            auto* block = slot(core, ticket);
            if (block->ticket != ticket || block->claimed ||
                block->state != static_cast<std::uint32_t>(stageState(stage))) break;
            const bool cancelled = block->flags & static_cast<std::uint32_t>(BlockFlag::Cancelled);
            finishBlock(core, *block, stage, cancelled, AudioErrorCode::None);
            --budget;
            progressed = true;
        }
    }
    return progressed;
}

bool publishBoundaries(QueueCore& core, unsigned& budget)
{
    // 队列满时保留待提交尾标，等待已占槽位归还后由 pump 再试。
    bool progressed = false;
    auto& h = *core.header;
    for (auto& pending : core.pendingBoundaries) {
        if (!pending || !budget || h.writeTicket - h.reclaimTicket >= h.slotCount) continue;
        const auto& info = *pending;
        const auto ticket = h.writeTicket++;
        auto* block = slot(core, ticket);
        std::memset(block, 0, sizeof(*block));
        block->ticket = ticket;
        block->sourceSequence = core.stats.load()->sequence[sourceIndex(info.source)]++;
        block->captureTimestampNs = info.timestampNs; block->pcmTimestampNs = info.timestampNs;
        block->epoch = info.epoch; block->source = static_cast<std::uint32_t>(info.source);
        block->kind = static_cast<std::uint32_t>(info.kind); block->format = info.inputFormat;
        if (info.cancelled) block->flags |= static_cast<std::uint32_t>(BlockFlag::Cancelled);
        if (info.error != AudioErrorCode::None) {
            block->flags |= static_cast<std::uint32_t>(BlockFlag::Dropped);
            block->errorCode = static_cast<std::uint32_t>(info.error);
        }
        block->state = static_cast<std::uint32_t>(BlockState::Captured);
        if (info.kind == BlockKind::StreamEnd) core.streamEndsQueued |= info.source;
        else core.segmentEndsQueued |= info.source;
        pending.reset();
        ++h.revision;
        --budget;
        progressed = true;
    }
    return progressed;
}
}

struct SharedPcmQueue::Private {
    std::shared_ptr<QueueCore> core = std::make_shared<QueueCore>();
    QTimer* timer = nullptr;
};
struct BlockView::Private {
    std::shared_ptr<QueueCore> core;
    CaptureId captureId;
    std::uint64_t ticket = 0;
    Stage stage = Stage::None;
    SharedBlockHeader snapshot{};
    std::uint8_t* bytes = nullptr;
    std::uint32_t capacity = 0;
};

template <typename T>
static void abandon(std::unique_ptr<T>& view)
{
    // 持有者没有显式 commit/fail/release 就析构视图时，按处理失败交接，避免队列永久卡住。
    if (!view || !view->core) return;
    auto core = view->core;
    Callback callback;
    CaptureId id;
    quint64 revision = 0;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (idMatches(*core, view->captureId)) {
            auto* block = slot(*core, view->ticket);
            if (block->ticket == view->ticket && block->claimed && block->state == static_cast<std::uint32_t>(stageState(view->stage))) {
                finishBlock(*core, *block, view->stage, true, AudioErrorCode::ProcessingFailed);
                callback = core->callback;
                id = core->info.captureId;
                revision = core->header->revision;
            }
        }
    }
    view.reset();
    notify(callback, id, revision);
}

BlockView::BlockView() : d(std::make_unique<Private>()) {}
BlockView::~BlockView() { abandon(d); }
BlockView::BlockView(BlockView&&) noexcept = default;
BlockView& BlockView::operator=(BlockView&& other) noexcept
{
    if (this != &other) { abandon(d); d = std::move(other.d); }
    return *this;
}
const CaptureId& BlockView::captureId() const { return d->captureId; }
Stage BlockView::stage() const { return d->stage; }
const SharedBlockHeader& BlockView::header() const { return d->snapshot; }
std::uint8_t* BlockView::data() { return d ? d->bytes : nullptr; }
const std::uint8_t* BlockView::data() const { return d ? d->bytes : nullptr; }
std::uint32_t BlockView::capacity() const { return d ? d->capacity : 0; }

SharedPcmQueue::SharedPcmQueue() : d(std::make_unique<Private>())
{
    d->timer = new QTimer(this);
    d->timer->setInterval(10);
    connect(d->timer, &QTimer::timeout, this, &SharedPcmQueue::pump);
}
SharedPcmQueue::~SharedPcmQueue() = default;

QueueResult SharedPcmQueue::create(const QueueConfig& config, const CaptureId& id,
                                   std::chrono::steady_clock::time_point origin)
{
    const QUuid uuid(id);
    if (uuid.isNull() || !config.slotCount || !config.payloadCapacity ||
        (config.enabledStages.toInt() & ~15u))
        return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
    const std::uint64_t stride = (std::uint64_t(sizeof(SharedBlockHeader)) + config.payloadCapacity + 63) & ~std::uint64_t(63);
    const std::uint64_t size = sizeof(SharedQueueHeader) + stride * config.slotCount;
    if (stride > std::numeric_limits<std::uint32_t>::max() || size > std::uint64_t(std::numeric_limits<int>::max()))
        return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (core->header) return result(QueueStatus::Error, AudioErrorCode::Busy);
    const QString key = QStringLiteral("chunfe-audio-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Qt's legacy QString key may select System V IPC on macOS, where the
    // default 8 MiB queue can exceed the system segment-size limit.
    core->memory.setNativeKey(QSharedMemory::platformSafeKey(key));
    if (!core->memory.create(static_cast<qsizetype>(size)))
        return result(QueueStatus::Error, AudioErrorCode::SharedMemoryFailed, core->memory.errorString());
    std::memset(core->memory.data(), 0, static_cast<std::size_t>(size));
    core->header = static_cast<SharedQueueHeader*>(core->memory.data());
    auto& h = *core->header;
    h.magic = SharedQueueMagic; h.version = SharedQueueVersion;
    h.headerBytes = sizeof(SharedQueueHeader); h.slotCount = config.slotCount;
    h.slotStride = static_cast<std::uint32_t>(stride); h.payloadOffset = sizeof(SharedBlockHeader);
    h.payloadCapacity = config.payloadCapacity; h.state = static_cast<std::uint32_t>(QueueState::Open);
    h.requestedSources = 3; h.acceptingSources = 3;
    h.reserved[0] = config.enabledStages.toInt();
    const QByteArray bytes = uuid.toRfc4122();
    std::memcpy(h.captureId, bytes.constData(), sizeof(h.captureId));
    core->info = {id, core->memory.nativeIpcKey().toString(), SharedQueueVersion,
                  size, config.slotCount, config.payloadCapacity};
    core->activeStats = std::make_unique<SourceStats>(id);
    core->stats.store(core->activeStats.get());
    if (core->statsReaders.load() == 0) core->retiredStats.clear();
    core->sourceFormats = {};
    core->origin = origin;
    core->pendingBoundaries = {};
    core->streamEndsQueued = {}; core->segmentEndsQueued = {};
    core->finishKind.reset(); core->completionSent = false;
    core->issues.clear(); core->reportedIssues = 0;
    ++core->incarnation;
    core->notifiedRevision = std::numeric_limits<quint64>::max();
    d->timer->start();
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::close()
{
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (!core->header) return result(QueueStatus::Ok);
    if (core->header->reclaimTicket != core->header->writeTicket || hasPendingBoundaries(*core) ||
        (core->finishKind && !core->completionSent))
        return result(QueueStatus::Error, AudioErrorCode::Busy);
    if (!core->memory.detach()) return result(QueueStatus::Error, AudioErrorCode::SharedMemoryFailed, core->memory.errorString());
    core->header = nullptr;
    core->stats.store(nullptr);
    retireStats(*core);
    ++core->incarnation;
    d->timer->stop();
    return result(QueueStatus::Ok);
}

AcquireStatus SharedPcmQueue::writeCaptured(const CaptureId& id, AudioSource source,
                                            const PcmInfo& input, const void* data)
{
    const int index = sourceIndex(source);
    if (index < 0 || !data || !validPcm(input) || !input.frameCount)
        return AcquireStatus::Error;
    auto core = d->core;
    StatsGuard statsGuard(*core);
    auto* stats = statsGuard.stats;
    if (!stats) return AcquireStatus::Closed;
    if (stats->captureId != id) return AcquireStatus::Error;
    if (!(stats->acceptingSources.load(std::memory_order_acquire) & static_cast<std::uint32_t>(source)))
        return AcquireStatus::Closed;
    const auto sequence = stats->sequence[index]++;
    // 回调不能等待队列锁；竞争和队列满均计入丢帧，后续成功块会携带缺口。
    std::unique_lock<std::mutex> guard(core->mutex, std::try_to_lock);
    if (!guard.owns_lock()) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return AcquireStatus::Full;
    }
    // The same capture ID can be reused after close/create. A producer that
    // held the old statistics must never write into the new mapping.
    if (core->stats.load() != stats || !idMatches(*core, id)) return AcquireStatus::Error;
    auto& h = *core->header;
    if (!(h.acceptingSources & static_cast<std::uint32_t>(source))) return AcquireStatus::Closed;
    if (input.validBytes > h.payloadCapacity) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return AcquireStatus::Error;
    }
    if (h.writeTicket - h.reclaimTicket >= h.slotCount) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return AcquireStatus::Full;
    }
    const auto ticket = h.writeTicket++;
    auto* block = slot(*core, ticket);
    std::memset(block, 0, sizeof(*block));
    block->ticket = ticket; block->sourceSequence = sequence;
    block->captureTimestampNs = input.timestampNs; block->pcmTimestampNs = input.timestampNs;
    block->source = static_cast<std::uint32_t>(source); block->epoch = h.epoch;
    block->sourceFrameCount = input.frameCount; block->frameCount = input.frameCount;
    block->validBytes = input.validBytes; block->format = input.format;
    block->droppedSourceFramesBefore = stats->pendingDroppedFrames[index].exchange(0);
    if (block->droppedSourceFramesBefore) block->flags |= static_cast<std::uint32_t>(BlockFlag::Gap);
    std::memcpy(payload(*core, block), data, input.validBytes);
    block->state = static_cast<std::uint32_t>(BlockState::Captured);
    ++h.revision;
    return AcquireStatus::Acquired;
}

AcquireResult SharedPcmQueue::tryReserve(const CaptureId& id, AudioSource source, const PcmInfo& input)
{
    const int index = sourceIndex(source);
    if (index < 0 || !validPcm(input) || !input.frameCount) return acquireError(AcquireStatus::Error, AudioErrorCode::InvalidArgument);
    auto core = d->core;
    StatsGuard statsGuard(*core);
    auto* stats = statsGuard.stats;
    if (!stats) return acquireError(AcquireStatus::Closed);
    if (stats->captureId != id) return acquireError(AcquireStatus::Error, AudioErrorCode::StaleCapture);
    if (!(stats->acceptingSources.load() & static_cast<std::uint32_t>(source))) return acquireError(AcquireStatus::Closed);
    const auto sourceSequence = stats->sequence[index]++;
    std::unique_lock<std::mutex> guard(core->mutex, std::try_to_lock);
    if (!guard.owns_lock()) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return acquireError(AcquireStatus::Full, AudioErrorCode::QueueFull);
    }
    if (!core->header) return acquireError(AcquireStatus::Closed);
    if (core->stats.load() != stats || !idMatches(*core, id))
        return acquireError(AcquireStatus::Error, AudioErrorCode::StaleCapture);
    auto& h = *core->header;
    if (!(h.acceptingSources & static_cast<std::uint32_t>(source))) return acquireError(AcquireStatus::Closed);
    if (input.validBytes > h.payloadCapacity) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return acquireError(AcquireStatus::Error, AudioErrorCode::BlockTooLarge);
    }
    if (h.writeTicket - h.reclaimTicket >= h.slotCount) {
        stats->pendingDroppedFrames[index] += input.frameCount;
        stats->totalDroppedFrames[index] += input.frameCount;
        return acquireError(AcquireStatus::Full, AudioErrorCode::QueueFull);
    }
    const auto ticket = h.writeTicket++;
    auto* block = slot(*core, ticket);
    std::memset(block, 0, sizeof(*block));
    block->ticket = ticket; block->sourceSequence = sourceSequence;
    block->captureTimestampNs = input.timestampNs; block->pcmTimestampNs = input.timestampNs;
    block->source = static_cast<std::uint32_t>(source); block->epoch = h.epoch;
    block->sourceFrameCount = input.frameCount; block->format = input.format;
    block->droppedSourceFramesBefore = stats->pendingDroppedFrames[index].exchange(0);
    if (block->droppedSourceFramesBefore) block->flags |= static_cast<std::uint32_t>(BlockFlag::Gap);
    block->state = static_cast<std::uint32_t>(BlockState::Free); block->claimed = 1;
    BlockView view;
    view.d->core = core; view.d->captureId = id; view.d->ticket = ticket;
    view.d->stage = Stage::Capture; view.d->snapshot = *block;
    view.d->bytes = payload(*core, block); view.d->capacity = h.payloadCapacity;
    AcquireResult result; result.status = AcquireStatus::Acquired; result.block = std::move(view);
    return result;
}

AcquireResult SharedPcmQueue::tryAcquire(const CaptureId& id, Stage stage)
{
    const int index = stageIndex(stage);
    if (index < 0) return acquireError(AcquireStatus::Error, AudioErrorCode::InvalidArgument);
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (!core->header) return acquireError(AcquireStatus::Closed);
    if (!idMatches(*core, id)) return acquireError(AcquireStatus::Error, AudioErrorCode::StaleCapture);
    auto& h = *core->header;
    if (!(h.reserved[0] & (1u << index)))
        return acquireError(AcquireStatus::Error, AudioErrorCode::InvalidState);
    // 每个阶段只处理自己游标指向的 ticket，前一块未交接时不能越过它。
    auto ticket = h.stageTicket[index];
    if (ticket >= h.writeTicket) return acquireError(AcquireStatus::WouldBlock);
    auto* block = slot(*core, ticket);
    if (block->ticket != ticket || block->claimed || block->state != static_cast<std::uint32_t>(stageState(stage)))
        return acquireError(AcquireStatus::WouldBlock);
    block->claimed = 1;
    BlockView view;
    view.d->core = core; view.d->captureId = id; view.d->ticket = ticket; view.d->stage = stage;
    view.d->snapshot = *block; view.d->bytes = payload(*core, block); view.d->capacity = h.payloadCapacity;
    AcquireResult result; result.status = AcquireStatus::Acquired; result.block = std::move(view);
    return result;
}

QueueResult SharedPcmQueue::commit(BlockView& view, const PcmInfo& output)
{
    if (!view.d || view.d->core != d->core) return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
    auto core = d->core;
    Callback callback; CaptureId id; quint64 revision = 0; bool cancelled = false;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!idMatches(*core, view.d->captureId)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
        auto* block = slot(*core, view.d->ticket);
        if (block->ticket != view.d->ticket || !block->claimed || block->state != static_cast<std::uint32_t>(stageState(view.d->stage)))
            return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
        if (view.d->stage == Stage::Asr) return result(QueueStatus::Error, AudioErrorCode::InvalidState);
        cancelled = block->flags & static_cast<std::uint32_t>(BlockFlag::Cancelled);
        // 持有期间发生取消时仍须归还处理权，但不能把已取消片段的输出发布给下游。
        if (!cancelled && (!validPcm(output) || output.validBytes > core->header->payloadCapacity))
            return result(QueueStatus::Error, output.validBytes > core->header->payloadCapacity ? AudioErrorCode::BlockTooLarge : AudioErrorCode::InvalidArgument);
        if (!cancelled) {
            block->format = output.format; block->frameCount = output.frameCount;
            block->validBytes = output.validBytes; block->pcmTimestampNs = output.timestampNs;
        }
        finishBlock(*core, *block, view.d->stage, cancelled, AudioErrorCode::None);
        callback = core->callback; id = core->info.captureId; revision = core->header->revision;
        view.d.reset();
    }
    notify(callback, id, revision);
    return result(cancelled ? QueueStatus::Cancelled : QueueStatus::Ok);
}

QueueResult SharedPcmQueue::fail(BlockView& view, AudioErrorCode reason)
{
    if (!view.d || view.d->core != d->core) return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
    auto core = d->core; Callback callback; CaptureId id; quint64 revision = 0;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!idMatches(*core, view.d->captureId)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
        auto* block = slot(*core, view.d->ticket);
        if (block->ticket != view.d->ticket || !block->claimed || block->state != static_cast<std::uint32_t>(stageState(view.d->stage)))
            return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
        finishBlock(*core, *block, view.d->stage, true, reason);
        callback = core->callback; id = core->info.captureId; revision = core->header->revision;
        view.d.reset();
    }
    notify(callback, id, revision);
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::release(BlockView& view)
{
    if (!view.d || view.d->core != d->core) return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
    auto core = d->core; Callback callback; CaptureId id; quint64 revision = 0; bool cancelled = false;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!idMatches(*core, view.d->captureId)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
        auto* block = slot(*core, view.d->ticket);
        if (block->ticket != view.d->ticket || !block->claimed || block->state != static_cast<std::uint32_t>(stageState(view.d->stage)))
            return result(QueueStatus::Error, AudioErrorCode::StaleBlock);
        if (view.d->stage != Stage::Asr) return result(QueueStatus::Error, AudioErrorCode::InvalidState);
        cancelled = block->flags & static_cast<std::uint32_t>(BlockFlag::Cancelled);
        finishBlock(*core, *block, Stage::Asr, cancelled, AudioErrorCode::None);
        callback = core->callback; id = core->info.captureId; revision = core->header->revision;
        view.d.reset();
    }
    notify(callback, id, revision);
    return result(cancelled ? QueueStatus::Cancelled : QueueStatus::Ok);
}

QueueSnapshot SharedPcmQueue::snapshot() const
{
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    QueueSnapshot s;
    s.info = core->info;
    if (!core->header) return s;
    auto& h = *core->header;
    s.state = static_cast<QueueState>(h.state); s.acceptingSources = AudioSources::fromInt(h.acceptingSources);
    s.stopMode = static_cast<StopMode>(h.stopMode); s.epoch = h.epoch;
    s.usedSlots = static_cast<std::uint32_t>(h.writeTicket - h.reclaimTicket);
    s.writeTicket = h.writeTicket; s.reclaimTicket = h.reclaimTicket; s.revision = h.revision;
    std::copy(std::begin(h.stageTicket), std::end(h.stageTicket), s.stageTicket);
    const auto stats = core->stats.load();
    if (stats) for (int i = 0; i < 2; ++i) s.droppedSourceFrames[i] = stats->totalDroppedFrames[i].load();
    if (!core->issues.isEmpty()) s.lastError = core->issues.last().error;
    return s;
}
void SharedPcmQueue::setUpdateCallback(std::function<void(const CaptureId&, quint64)> callback)
{
    std::lock_guard<std::mutex> guard(d->core->mutex);
    d->core->callback = std::move(callback);
}

QueueResult SharedPcmQueue::seal(const CaptureId& id, AudioSources sources, StopMode mode)
{
    auto core = d->core; Callback callback; quint64 revision = 0;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!idMatches(*core, id)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
        if (sources.toInt() & ~3u) return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
        if (core->finishKind == BlockKind::StreamEnd) {
            if (core->finishMode == StopMode::Discard) mode = StopMode::Discard;
            core->finishMode = mode;
        }
        sealLocked(*core, sources, mode);
        revision = core->header->revision; callback = core->callback;
    }
    notify(callback, id, revision);
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::resumeEpoch(const CaptureId& id, std::uint32_t epoch, AudioSources sources)
{
    auto core = d->core; Callback callback; quint64 revision = 0;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!idMatches(*core, id)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
        auto& h = *core->header;
        if (sources.toInt() & ~3u || !sources.toInt() || epoch < h.epoch)
            return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
        if (core->finishKind == BlockKind::StreamEnd)
            return result(QueueStatus::Error, AudioErrorCode::InvalidState);
        // 旧片段的尾标和持有块必须先完成，避免新 epoch 与旧数据共用槽位。
        if (hasPendingBoundaries(*core) || (core->finishKind && !core->completionSent) ||
            (epoch && h.reclaimTicket != h.writeTicket))
            return result(QueueStatus::Error, AudioErrorCode::Busy);
        h.epoch = epoch; h.requestedSources = static_cast<std::uint32_t>(sources.toInt());
        h.acceptingSources = h.requestedSources; h.state = static_cast<std::uint32_t>(QueueState::Open);
        if (const auto stats = core->stats.load()) {
            stats->acceptingSources = h.acceptingSources;
            if (epoch) for (auto& pending : stats->pendingDroppedFrames) pending = 0;
        }
        h.stopMode = static_cast<std::uint32_t>(StopMode::Drain);
        core->segmentEndsQueued = {};
        core->finishKind.reset(); core->completionSent = false;
        revision = ++h.revision; callback = core->callback;
    }
    notify(callback, id, revision);
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::setSourceFormat(const CaptureId& id, AudioSource source, const PcmFormat& format)
{
    if (sourceIndex(source) < 0 || !validPcm({format, 0, 0, 0}))
        return result(QueueStatus::Error, AudioErrorCode::UnsupportedFormat);
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (!idMatches(*core, id)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
    core->sourceFormats[sourceIndex(source)] = format;
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::finish(const CaptureId& id, AudioSources sources, BlockKind kind, StopMode mode)
{
    if ((sources.toInt() & ~3u) || (kind != BlockKind::SegmentEnd && kind != BlockKind::StreamEnd))
        return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (!idMatches(*core, id)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
    if (core->finishKind == BlockKind::StreamEnd && kind == BlockKind::SegmentEnd)
        return result(QueueStatus::Error, AudioErrorCode::InvalidState);
    for (auto source : {AudioSource::Mic, AudioSource::Spk}) {
        if (sources.testFlag(source) && !core->streamEndsQueued.testFlag(source) &&
            !validPcm({core->sourceFormats[sourceIndex(source)], 0, 0, 0}))
            return result(QueueStatus::Error, AudioErrorCode::UnsupportedFormat);
    }
    if (core->finishKind == kind && core->finishMode == StopMode::Discard)
        mode = StopMode::Discard;
    const bool upgraded = core->finishKind != kind;
    core->finishKind = kind; core->finishMode = mode;
    if (upgraded) core->completionSent = false;
    sealLocked(*core, AudioSources::fromInt(3), mode);
    for (auto source : {AudioSource::Mic, AudioSource::Spk})
        if (sources.testFlag(source)) queueBoundary(*core, source, kind, mode);
    // 这里只登记请求；pump 异步发布尾标并等待启用阶段确认，避免同步完成信号重入调用方。
    return result(QueueStatus::Ok);
}

QueueResult SharedPcmQueue::finishSource(const CaptureId& id, AudioSource source, AudioErrorCode reason)
{
    if (sourceIndex(source) < 0) return result(QueueStatus::Error, AudioErrorCode::InvalidArgument);
    auto core = d->core;
    std::lock_guard<std::mutex> guard(core->mutex);
    if (!idMatches(*core, id)) return result(QueueStatus::Error, AudioErrorCode::StaleCapture);
    if (!validPcm({core->sourceFormats[sourceIndex(source)], 0, 0, 0}))
        return result(QueueStatus::Error, AudioErrorCode::UnsupportedFormat);
    const auto mode = core->finishKind ? core->finishMode : StopMode::Drain;
    sealLocked(*core, AudioSources(source), mode);
    queueBoundary(*core, source, BlockKind::StreamEnd, mode, reason);
    return result(QueueStatus::Ok);
}

void SharedPcmQueue::pump()
{
    const QPointer<SharedPcmQueue> alive(this);
    auto core = d->core;
    CaptureId id;
    quint64 incarnation = 0, revision = 0;
    std::uint32_t epoch = 0;
    bool changed = false;
    QList<AudioIssue> issues;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        if (!core->header) return;
        // 每次事件循环限制工作量；实时音频回调不执行旁路或收尾循环。
        unsigned budget = 1024;
        bool progressed = true;
        while (budget && progressed) {
            progressed = bypassDisabledStages(*core, budget);
            progressed = publishBoundaries(*core, budget) || progressed;
        }
        id = core->info.captureId; incarnation = core->incarnation; epoch = core->header->epoch;
        revision = core->header->revision;
        changed = revision != core->notifiedRevision;
        core->notifiedRevision = revision;
        while (core->reportedIssues < core->issues.size())
            issues.append(core->issues[core->reportedIssues++]);
    }
    // 信号槽可能重入、关闭队列或创建新会话；每次通知后都核对对象和本轮队列身份。
    auto current = [&] {
        std::lock_guard<std::mutex> guard(core->mutex);
        return idMatches(*core, id) && core->incarnation == incarnation && core->header->epoch == epoch;
    };
    if (changed) emit updated(id, revision);
    if (!alive || !current()) return;
    for (const auto& issue : issues) {
        emit issueReported(issue);
        if (!alive || !current()) return;
    }

    std::optional<BlockKind> completed;
    DrainResult drain;
    {
        std::lock_guard<std::mutex> guard(core->mutex);
        // 排空不仅要求当前没有块，还要等待待入队尾标和所有阶段的处理权归还。
        if (!idMatches(*core, id) || core->incarnation != incarnation || core->header->epoch != epoch ||
            !core->finishKind || core->completionSent || hasPendingBoundaries(*core) ||
            core->header->writeTicket != core->header->reclaimTicket) return;
        completed = core->finishKind;
        core->completionSent = true;
        drain.mode = core->finishMode;
        drain.cancelled = drain.mode == StopMode::Discard;
        drain.releasedBlocks = core->header->reclaimTicket;
        drain.issues = core->issues;
        if (const auto stats = core->stats.load())
            for (int i = 0; i < 2; ++i) drain.droppedSourceFrames[i] = stats->totalDroppedFrames[i].load();
    }
    if (completed == BlockKind::SegmentEnd) emit segmentFinished(id, epoch);
    else emit drained(id, drain);
}
} // namespace AudioPipline
