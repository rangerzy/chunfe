#ifndef CHUNFE_SHARED_PCM_QUEUE_H
#define CHUNFE_SHARED_PCM_QUEUE_H

#include "audio_pipline_types.h"

#include <QObject>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace AudioPipline {

inline constexpr std::uint32_t SharedQueueMagic = 0x31515041u; // APQ1，小端。
inline constexpr std::uint32_t SharedQueueVersion = 1;

// 仅固定宽度字段。同步字段须经队列内部原子/互斥协议访问，不能直接读写。
struct alignas(64) SharedQueueHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t headerBytes;
    std::uint32_t slotCount;
    std::uint32_t slotStride;
    std::uint32_t payloadOffset;
    std::uint32_t payloadCapacity;
    std::uint32_t state;             // QueueState
    std::uint8_t captureId[16];      // UUID 的固定字节表示，不是 QString。
    std::uint64_t writeTicket;
    std::uint64_t stageTicket[4];    // Convert / Resample / Save / Asr
    std::uint64_t reclaimTicket;
    std::uint64_t revision;
    std::uint32_t epoch;
    std::uint32_t requestedSources;
    std::uint32_t acceptingSources; // 暂停或封口时为 0，控制块仍可提交。
    std::uint32_t stopMode;         // Drain 保留音频；Discard 取消当前片段。
    std::uint32_t reserved[2];       // reserved[0]: enabled ProcessingStages mask.
};

// PCM 位于块头后 payloadOffset 字节处，各阶段修改的是同一槽位。
struct alignas(64) SharedBlockHeader {
    std::uint64_t ticket;
    std::uint64_t sourceSequence;
    std::int64_t captureTimestampNs;
    std::int64_t pcmTimestampNs;
    std::uint64_t droppedSourceFramesBefore;
    std::uint32_t epoch;
    std::uint32_t source;            // AudioSource
    std::uint32_t kind;              // BlockKind
    std::uint32_t state;             // 五个 BlockState 之一。
    std::uint32_t claimed;           // 内部占用保护，0/1；不是新的业务状态。
    std::uint32_t flags;             // BlockFlag 位集合。
    std::uint32_t errorCode;         // AudioErrorCode
    std::uint32_t sourceFrameCount;
    std::uint32_t frameCount;
    std::uint32_t validBytes;
    PcmFormat format;
    std::uint32_t reserved[8];
};

static_assert(sizeof(PcmFormat) == 16);
static_assert(std::is_standard_layout_v<PcmFormat> && std::is_trivially_copyable_v<PcmFormat>);
static_assert(sizeof(SharedQueueHeader) == 128 && alignof(SharedQueueHeader) == 64);
static_assert(sizeof(SharedBlockHeader) == 128 && alignof(SharedBlockHeader) == 64);
static_assert(std::is_standard_layout_v<SharedQueueHeader> && std::is_trivially_copyable_v<SharedQueueHeader>);
static_assert(std::is_standard_layout_v<SharedBlockHeader> && std::is_trivially_copyable_v<SharedBlockHeader>);
static_assert(offsetof(SharedQueueHeader, captureId) == 32);
static_assert(offsetof(SharedQueueHeader, writeTicket) == 48);
static_assert(offsetof(SharedBlockHeader, claimed) == 56);
static_assert(offsetof(SharedBlockHeader, format) == 80);

class SharedPcmQueue;

// 进程内的独占处理权，不可复制。成功交接/释放后地址和视图立即失效；
// 若直接析构仍有效的视图，队列会按处理失败交接该块。
class AUDIO_PIPLINE_EXPORT BlockView {
public:
    ~BlockView();
    BlockView(BlockView&&) noexcept;
    BlockView& operator=(BlockView&&) noexcept;
    BlockView(const BlockView&) = delete;
    BlockView& operator=(const BlockView&) = delete;

    const CaptureId& captureId() const;
    Stage stage() const;
    // 这是取得处理权时的块头快照；后续取消以交接时的返回状态为准。
    const SharedBlockHeader& header() const;
    std::uint8_t* data();
    const std::uint8_t* data() const;
    std::uint32_t capacity() const;

private:
    friend class SharedPcmQueue;
    BlockView();
    struct Private;
    std::unique_ptr<Private> d;
};

enum class AcquireStatus { Acquired, WouldBlock, Full, Closed, Error };
struct AcquireResult {
    AcquireStatus status = AcquireStatus::Error;
    std::optional<BlockView> block;
    AudioError error;
};

// 队列在普通音频封口后，仍通过相同的槽位协议提交结束标记。
struct BoundaryInfo {
    CaptureId captureId;
    AudioSource source = AudioSource::None;
    BlockKind kind = BlockKind::StreamEnd;
    std::uint32_t epoch = 0;
    bool cancelled = false;
    AudioErrorCode error = AudioErrorCode::None;
    std::int64_t timestampNs = 0;
    PcmFormat inputFormat{};
};

class AUDIO_PIPLINE_EXPORT SharedPcmQueue : public QObject {
    Q_OBJECT
public:
    SharedPcmQueue();
    ~SharedPcmQueue() override;
    SharedPcmQueue(const SharedPcmQueue&) = delete;
    SharedPcmQueue& operator=(const SharedPcmQueue&) = delete;

    // 生命周期命令在队列所属线程调用；PCM 和阶段处理方法可跨线程调用。
    QueueResult create(const QueueConfig& config, const CaptureId& captureId,
                       std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now());
    QueueResult close(); // 在用块未归还时返回 Busy，不能强制解除映射。
    // 先封禁所选来源的写入；Discard 标记已有数据取消，但不抢占正在处理的块。
    QueueResult seal(const CaptureId& id, AudioSources sources, StopMode mode);
    // 暂停片段已排空后开启新 epoch；最终结束的流不能再恢复。
    QueueResult resumeEpoch(const CaptureId& id, std::uint32_t epoch, AudioSources sources);
    // 登记实际输入格式，供队列构造该来源的结束标记。
    QueueResult setSourceFormat(const CaptureId& id, AudioSource source, const PcmFormat& format);
    // 生产者停稳后请求片段或整个音频流收尾；队列异步插入尾标，完成由信号通知。
    QueueResult finish(const CaptureId& id, AudioSources sources, BlockKind kind, StopMode mode);
    // 单路故障停稳后发送带原因的 StreamEnd，不终止其他来源。
    QueueResult finishSource(const CaptureId& id, AudioSource source, AudioErrorCode reason);
    // 实时采集专用：一次非阻塞预留，直接复制并提交；通知由队列所属线程异步发送。
    // 每个来源只能有一个生产者以保证序号顺序；MIC 和 SPK 可以并发写入。
    AcquireStatus writeCaptured(const CaptureId& id, AudioSource source,
                                const PcmInfo& input, const void* data);
    // 取得采集槽位后由调用者填充 PCM 并提交；取得失败不返回可用视图。
    AcquireResult tryReserve(const CaptureId& id, AudioSource source, const PcmInfo& input);
    // 只领取已启用阶段的下一个就绪块；未就绪时返回 WouldBlock。
    AcquireResult tryAcquire(const CaptureId& id, Stage stage);
    // 校验失败保留有效处理权；已取消的块则归还处理权并返回 Cancelled。
    QueueResult commit(BlockView& block, const PcmInfo& output);
    QueueResult fail(BlockView& block, AudioErrorCode reason);
    QueueResult release(BlockView& block); // 仅 ASR；不再引用 PCM 才能归还。
    QueueSnapshot snapshot() const;
    // 状态发布后在调用线程执行；回调应尽快返回，不得持有数据指针。
    void setUpdateCallback(std::function<void(const CaptureId&, quint64)> callback);

    // tryAcquire 原子核对 ID/ticket/state/claimed；各阶段按自己的游标推进。
    // commit 固定推进一步，ASR 用 release；fail 在 ASR 阶段直接最终释放。
    // 启用阶段必须 commit/release；禁用阶段由队列旁路。满队列不覆盖未释放槽位。
    // 提交元信息/状态/修订号后，通知所有处理阶段重新检查队列。

signals:
    // updated 只提示重新检查快照，不与数据块一一对应。
    void updated(AudioPipline::CaptureId id, quint64 revision);
    // 分别表示本片段或整个流的尾标已由启用阶段确认，且所有块已归还。
    void segmentFinished(AudioPipline::CaptureId id, std::uint32_t epoch);
    void drained(AudioPipline::CaptureId id, AudioPipline::DrainResult result);
    void issueReported(AudioPipline::AudioIssue issue);

private:
    void pump();
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace AudioPipline

#endif // CHUNFE_SHARED_PCM_QUEUE_H
