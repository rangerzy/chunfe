# AudioPipline 单共享队列接口设计

> 历史设计。2026-11-13 已移除共享内存接口，当前采集契约见 [AudioPipline 模块说明](../../../plugins/AudioPipline/README.md)。

日期：2026-11-11，职责边界更新于 2026-11-12。单共享内存队列、同一音频块逐阶段原地处理的模型已经用户确认。接口与实现位于 `plugins/AudioPipline/`。本文记录接口契约，代码片段为声明摘录或协议示意；转换、重采样、保存和 ASR 算法尚未接入。

## 模型与职责

```text
MIC / SPK → memcpy 到共享槽位 → 格式转换 → 降采样 → 可选压缩保存 → ASR → 槽位复用
```

全程一个共享内存环形队列。各阶段修改同一槽位中的 PCM，提交完成标记并通知下一阶段。MIC/SPK 通过 source 区分。不同块可以在不同阶段并行，同一块只能由一个阶段修改。

2026-11-12 用户确认职责拆分：AudioPiplineManager 分别管理 MIC、SPK 两个单来源 AudioCaptureWorker，只负责发出命令、汇总回执及维护对外会话状态，包括跨来源启动/回滚和暂停/最终停止目标。worker 自行执行本路设备操作、停止失败重试、故障处理、电平统计、PCM 分块和直接入队；manager 不再把捕获自身状态的设备操作流程投递到 worker 线程。captures 层的共享上下文负责设备枚举、权限适配和原生后端上下文，具体设备资源由各 worker 持有；原生类型不进入 manager。两个 worker 继续共享同一个设备控制线程，串行执行后端调用。

SharedPcmQueue 自行执行阶段旁路、结束标记生成与入队重试、取消块交接和排空判定，manager 通过高层生命周期命令请求收尾并接收完成通知。转换、重采样、保存、ASR 工作线程只处理显式启用的阶段，通过同一个队列接口领取和交还数据；算法不进入采集回调。

压缩保存根据当前 PCM 生成文件或编码输出，队列中的负载继续保留 PCM。计算库允许使用线程内部临时工作空间，最终结果写回当前槽位，不新增阶段间队列。

manager 直接持有两个采集 worker、共享队列和控制线程，不通过 Runtime/PImpl 或另一层协调器汇总自身状态。直接成员记录会话、环境请求、启动阶段、待收回执和停止目标；后端停止重试留在 worker，槽位及尾标收尾状态留在 queue。停止完成时按最新目标进入 Paused 或最终进入 Idle，保证暂停停机过程中升级为最终停止仍能正确收尾。

worker 命令按值携带来源、captureId、操作编号及所需配置，回执带相同身份；manager 在自己的线程过滤过期回执并推进下一阶段。启动顺序为准备命令 → 汇总 prepared → 启动命令 → 汇总 started → 确认命令 → 汇总 confirmed → 对外 captureStarted → 激活命令。全部批准来源通过确认后才统一允许有效 PCM；恢复不重复发送 captureStarted。停止命令由 worker 自行清理与重试，停稳后才回复 stopped。单路故障、电平和问题也由 worker 上报；manager 汇总两路电平并限制对外通知频率。环境请求由 MIC worker 的共享环境请求入口执行，manager 保留请求 ID、取消和结果转发。

## 共享内存布局

```text
[SharedQueueHeader]
[SharedBlockHeader 0][PCM 槽位 0]
[SharedBlockHeader 1][PCM 槽位 1]
...
[SharedBlockHeader N-1][PCM 槽位 N-1]
```

下面的结构与头文件声明保持相同字段顺序、整数宽度和对齐。共享区不含指针、QString、QByteArray 或其他容器。共享控制字段仅通过队列封装的同步操作访问，不能把 volatile 或发信号当作内存同步。

当前草案版本为 1，尚无已部署的旧 ABI。区域头和块头各为 128 字节、按 64 字节对齐，PCM 起点也按 64 字节对齐；整数采用小端编码，magic 为字节序列 APQ1（`0x31515041u`）。头部预留和填充计入 headerBytes/payloadOffset，不能按字段大小之和猜测偏移。

```cpp
struct alignas(64) SharedQueueHeader {
    std::uint32_t magic;                 // APQ1
    std::uint32_t version;               // 1
    std::uint32_t headerBytes;
    std::uint32_t slotCount;
    std::uint32_t slotStride;            // 相邻块头间的字节距离
    std::uint32_t payloadOffset;         // PCM 相对块头的偏移
    std::uint32_t payloadCapacity;       // 每槽 PCM 最大字节数
    std::uint32_t state;                 // QueueState
    std::uint8_t captureId[16];          // 本次采集 UUID 的固定字节表示
    std::uint64_t writeTicket;           // 下一个成功预留的位置
    std::uint64_t stageTicket[4];        // Convert / Resample / Save / Asr 的下一位置
    std::uint64_t reclaimTicket;         // 最早尚未回收的位置
    std::uint64_t revision;              // 每次可观察变化递增
    std::uint32_t epoch;                 // 暂停恢复后的连续片段编号
    std::uint32_t requestedSources;      // 本次批准的 MIC/SPK 位集合
    std::uint32_t acceptingSources;      // 允许普通采集写入的来源位集合
    std::uint32_t stopMode;              // Drain 保留音频；Discard 取消当前片段
    std::uint32_t reserved[2];          // [0]: enabledStages 位掩码；[1] 保留为 0
};

struct PcmFormat {
    std::uint32_t sampleRate;            // Hz
    std::uint32_t channels;
    SampleType sampleType;               // uint32_t 枚举
    PcmLayout layout;                    // uint32_t 枚举
};

struct alignas(64) SharedBlockHeader {
    std::uint64_t ticket;                // 槽位本轮身份，区别同一位置的重复使用
    std::uint64_t sourceSequence;        // 每来源递增，包含未能入队的采集块
    std::int64_t captureTimestampNs;     // 原始采集时刻或结束边界时刻
    std::int64_t pcmTimestampNs;         // 当前 PCM 首样本时刻
    std::uint64_t droppedSourceFramesBefore;
    std::uint32_t epoch;
    std::uint32_t source;                // Mic / Spk
    std::uint32_t kind;                  // Audio / SegmentEnd / StreamEnd
    std::uint32_t state;                 // 五个 BlockState 之一
    std::uint32_t claimed;               // 内部独占标志，0/1
    std::uint32_t flags;                 // Gap / Dropped / Cancelled
    std::uint32_t errorCode;             // 无错误为 0
    std::uint32_t sourceFrameCount;      // 原采集每声道帧数，后续不修改
    std::uint32_t frameCount;            // 当前 PCM 每声道帧数
    std::uint32_t validBytes;            // 当前有效字节数
    PcmFormat format;                    // 当前 PCM 格式
    std::uint32_t reserved[8];
};
```

头文件以静态断言约束 `sizeof(PcmFormat) == 16`、两个头的大小和对齐，以及关键偏移：区域头 captureId=32、writeTicket=48；块头 claimed=56、format=80。

`SharedQueueHeader::reserved[0]` 保存本次队列冻结的处理阶段配置：Convert=1、Resample=2、Save=4、Asr=8，允许按位组合。0 明确表示全部处理阶段旁路；不是等待自动发现消费者。复用这一预留字段不改变区域头或块头的尺寸与偏移。

槽位地址为 `base + headerBytes + (ticket % slotCount) * slotStride`；PCM 地址在此基础上再加 payloadOffset。映射大小、偏移、容量和版本必须在打开时校验。

时间使用本次采集的统一单调时钟起点。ticket、sourceSequence、epoch、source 和 captureTimestampNs 由队列/采集端确定，后续阶段不可改写；处理者只能提交当前 PCM 的格式、帧数、有效长度及首样本时间。

PCM 的 Int16/Int24/Int32 分别使用有符号 2/3/4 字节样本，Float32 使用 4 字节 IEEE 754 样本；v1 使用小端布局。Interleaved 按帧排列各声道；Planar 按声道依次紧密排列等长平面，不存平面指针。`validBytes = frameCount × channels × bytesPerSample`，无额外填充。

共享枚举值固定：source 的 None/Mic/Spk 为 0/1/2（Mic、Spk 同时也是 sources 的位值）；sampleType 的 Unknown=0，Int16/Int24/Int32/Float32 依次为 1–4；layout 的 Unknown/Interleaved/Planar 为 0/1/2；kind 的 Audio/SegmentEnd/StreamEnd 为 0/1/2；flags 的 Gap/Dropped/Cancelled 为 1/2/4，允许按位组合。BlockState 的 Free/Captured/Converted/Resampled/ReadyForAsr 为 0–4；StopMode 的 Drain/Discard 为 0/1。

## 块状态和交接

```text
Free → Captured → Converted → Resampled → ReadyForAsr → Free
```

BlockState 只表示已完成的阶段；独占期间保持原状态，队列内部将 claimed 从 0 设为 1。成功提交或释放时更新状态并清除 claimed。claimed 是同步保护，不是额外的业务状态；取得处理权必须同时核对 captureId、ticket、状态和 claimed。

| 阶段 | 取得前状态（claimed=0） | 独占期间（claimed=1） | 提交/释放后状态（claimed=0） |
| --- | --- | --- | --- |
| 采集 | Free | Free | Captured |
| 格式转换 | Captured | Captured | Converted |
| 降采样 | Converted | Converted | Resampled |
| 可选保存 | Resampled | Resampled | ReadyForAsr |
| 转写输入 | ReadyForAsr | ReadyForAsr | Free |

`QueueConfig.enabledStages` 决定由谁推进阶段：显式启用的阶段必须由对应处理者取得块并 commit/fail/release；即使本块无需计算，仍需确认。禁用阶段由队列在自己的事件循环中按相同状态链逐级旁路，不等待不存在的消费者。启用的 ASR 在不再读取或引用槽位内存时释放；不要求等文字识别完成。queue 从不因为某阶段暂未领取块、音频被取消或等待过久而推断该阶段未启用，也不会强取 claimed 块。

每个阶段只处理自身 stageTicket 对应的块；一次最多持有一个块，提交或释放后推进该阶段游标。各来源的重采样和编码上下文分别维护。单队列采用全局 ticket 顺序，因此前面的慢块会使同阶段的后续块等待；不同阶段仍可以并行。

MIC/SPK 各有一个采集生产者，通过队列内部同步预留唯一 ticket。只能预留 writeTicket 对应的环形位置，不扫描其他空闲槽；成功预留才推进 writeTicket，memcpy 在取得槽位后进行。reclaimTicket 只越过已经最终释放的连续前缀。

写完负载和元信息后，再提交状态、更新 revision，最后发送通知。下一阶段取得块时必须看到完整已提交数据。处理权不会因为超时自动转移。

## 参数与返回值

```cpp
enum class ProcessingStage : uint32_t { Convert = 1, Resample = 2, Save = 4, Asr = 8 };
Q_DECLARE_FLAGS(ProcessingStages, ProcessingStage)

struct QueueConfig {
    uint32_t slotCount = 128;
    uint32_t payloadCapacity = 64 * 1024;
    ProcessingStages enabledStages{}; // 默认没有启用的下游阶段
};

struct DeviceSelection {
    DeviceSelectionMode mode = DeviceSelectionMode::DefaultAtStart;
    QString deviceId;               // Explicit 时必填；默认模式时为空
};

struct CaptureConfig {
    AudioSources sources;           // 非空：Mic / Spk / 两者
    DeviceSelection mic;
    DeviceSelection spk;
    QueueConfig queue;
};

struct PcmInfo {
    PcmFormat format;
    uint32_t frameCount;
    uint32_t validBytes;
    int64_t timestampNs;
};

struct StartResult {
    bool accepted;
    CaptureId captureId;            // 受理时有效，进程内使用 QString
    AudioError error;
};

struct CommandResult {
    bool accepted;
    AudioError error;
};
```

默认队列 PCM 容量为 8 MiB，另加头部。槽位大小必须覆盖源数据和各阶段中间结果的最大尺寸，不能只按降采样后的尺寸分配。采集回调过大时按完整音频帧拆成多个块；不能截断一个样本。处理模块在写入前检查输出上界，不允许写出槽位后才检测溢出。

当前应用尚未接入下游算法，缺省 `enabledStages={}`，普通音频与尾标都会自动经过全部禁用阶段并回收，因此 Drain 和 Discard 都可以在没有消费者时完成。接入某阶段前必须显式启用其位，并安装对应消费者；若已声明启用却没有消费者，queue 保留数据和尾标并等待确认，不会自动回收该阶段。这项静态配置取代隐式猜测消费者是否存在，采集中不动态修改。

采集端发布实际采集格式，不强制 16 kHz。转换/降采样的目标格式由处理模块配置，默认设备在启动时解析并固定，本次配置冻结，页面修改下次生效。

## SharedPcmQueue 方法

```cpp
QueueResult create(const QueueConfig&, const CaptureId&,
                   std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now());
QueueResult close(); // manager 在收到 drained 后调用
QueueResult seal(const CaptureId& id, AudioSources sources, StopMode mode);
QueueResult resumeEpoch(const CaptureId& id, uint32_t epoch, AudioSources sources);
QueueResult setSourceFormat(const CaptureId& id, AudioSource source, const PcmFormat& format);
QueueResult finish(const CaptureId& id, AudioSources sources, BlockKind kind, StopMode mode);
QueueResult finishSource(const CaptureId& id, AudioSource source, AudioErrorCode reason);

AcquireStatus writeCaptured(const CaptureId& id, AudioSource source, const PcmInfo& input, const void* data);
AcquireResult tryReserve(const CaptureId& id, AudioSource source, const PcmInfo& input);
AcquireResult tryAcquire(const CaptureId& id, Stage stage); // Convert / Resample / Save / Asr
QueueResult commit(BlockView& block, const PcmInfo& output);
QueueResult fail(BlockView& block, AudioErrorCode reason);
QueueResult release(BlockView& block); // 仅持有 ReadyForAsr 块的 ASR 阶段

QueueSnapshot snapshot() const;

// Qt signals，均由 queue 所属线程异步发布。
void updated(CaptureId id, quint64 revision);
void segmentFinished(CaptureId id, uint32_t epoch);
void drained(CaptureId id, DrainResult result);
void issueReported(AudioIssue issue);
```

| 方法 | 契约 |
| --- | --- |
| create | 分配唯一共享区域，冻结 enabledStages 并保存统一时钟起点；初始化本次队列状态和通知身份。 |
| close | 在采集停稳、所请求收尾已经完成且无待处理或持有块时释放区域；存在待入队尾标或未完成收尾也返回 Busy。队列不在 drained 信号内自行关闭。 |
| seal | 立即封禁指定来源的普通写入；Discard 标记对应数据和已请求的尾标取消，不抢占使用中的负载。此调用不表示后端已停稳。 |
| resumeEpoch | 旧片段已经完成且无持有或待处理块后开启新 epoch，重新允许所选来源写入；最终 StreamEnd 收尾不能恢复。 |
| setSourceFormat | 登记该来源的实际格式，供 queue 自行构造零负载尾标；暂停恢复时保留上次格式，支持恢复失败来源的最终收尾。 |
| finish | manager 确认全部相关 worker 停稳后，请求来源的 SegmentEnd 或 StreamEnd；queue 自行生成尾标、等待空位并追踪确认。返回受理结果，完成通知始终异步发送。尚未发布音频的启动回滚可传空 sources，无需伪造尾标。 |
| finishSource | 单路故障停稳后封禁该来源并请求带 reason 的 StreamEnd，使用已登记格式；兄弟来源继续运行，不单独触发全队列 drained。 |
| writeCaptured | 实时采集专用，一次非阻塞预留、复制并发布 Captured；不运行旁路/收尾循环，也不从音频回调发送 Qt 信号。 |
| tryReserve | 校验源音频格式和大小，尝试取得 Free 且 claimed=0 的块并设 claimed=1；不等待其他阶段。 |
| tryAcquire | 显式启用的阶段尝试取得下一个块并设 claimed=1，保持完成状态；没有就绪块返回 WouldBlock。对禁用阶段调用返回 Error/InvalidState。 |
| commit | 校验块身份、阶段、claimed、长度和格式，提交数据并固定推进一个阶段、清除 claimed，成功后 BlockView 失效。调用方不能任意指定跳转状态。 |
| fail | 持有者已停止访问负载后，将该块标记 Dropped/错误，清空有效 PCM，再按阶段交接。若持有者是 ASR 阶段，则直接完成错误记录与最终释放并推进 ASR/reclaim 游标。成功后 BlockView 失效。 |
| release | ASR 确认不再使用输入内存，完成该 ticket 并回收连续空闲前缀；之后 BlockView 失效。 |
| snapshot | 返回队列状态、容量、占用量、各阶段进度、溢出/丢失统计和问题摘要，不返回 PCM 副本。 |

tryReserve/tryAcquire 在取得处理权的同一同步边界内核对 captureId；不匹配返回 Error/StaleCapture，不读取或改动当前队列。工作线程不能只在处理通知前检查一次 ID，就省略取得块时的检查。

AcquireResult 区分 Acquired、WouldBlock、Full、Closed、Error。失败不返回可用数据地址；Full 表示普通采集块无法预留，Closed 表示当前不允许该操作。只有采集预留会返回 Full。

AcquireResult 包含 status、可选 BlockView 和 AudioError。QueueResult 包含 status（Ok / Cancelled / Error）及 AudioError；无错误时 error.code 为 None。Cancelled 表示有效处理权已被取消流程接收，BlockView 同样失效。普通 commit 不接受 ASR 阶段持有的块，ASR 只能 release 或 fail。Error 类型的提交校验失败保留原有效处理权；StaleBlock 则表示该句柄本来已失效，禁止继续访问。

BlockView 是进程内不可复制、可移动的临时对象，通过 captureId()、stage()、header()、data() 和 capacity() 提供身份、只读块头、当前槽位数据地址和容量；ticket 位于块头中。它不存入共享内存。持有期间 claimed=1；成功 commit/fail/release 后所有地址和视图立即失效，禁止继续访问。排队的旧通知、旧 ticket 或旧 captureId 不能取得或修改新数据。校验失败的提交不释放有效处理权，调用方应修正或 fail 后退出。

SharedPcmQueue 是无 QObject parent、由 shared_ptr 管理的 QObject。生命周期命令在其所属线程调用；PCM 写入、阶段领取/交还和 snapshot 使用队列内部同步供工作线程访问。其内部定时事件循环执行有界的旁路与尾标重试，并合并状态通知。所有入口遵守同一槽位同步规则；manager 不直接操作共享头，不再预留或提交边界 BlockView，也不遍历下游阶段代为释放数据。

尾标的来源、epoch、错误、时间戳和输入格式由 queue 根据 finish/finishSource 请求及登记信息构造，kind 只能为 SegmentEnd/StreamEnd；普通音频已封禁时仍可入队。队列满时保留待提交的尾标并异步重试，不计作音频丢失。重复收尾请求不重复发布同一来源的 StreamEnd；暂停可升级为最终停止，尚未入队的 SegmentEnd 改为 StreamEnd，已经发布的 SegmentEnd 保留并追加所需 StreamEnd。同一次最终停止可从 Drain 升级为 Discard，不能恢复为 Drain。

`segmentFinished(id, epoch)` 只在该片段的必要尾标均完成且无持有或待处理块时发送，不关闭共享区；`drained(id, result)` 只在最终收尾的必要 StreamEnd 均完成且全部槽位释放时发送。暂时为空不触发这两个信号。每次片段/最终收尾仅发送一次完成通知；create、resumeEpoch 和信号重入均核对队列身份及 epoch，旧完成事件不得作用于新片段或新会话。阶段 fail/放弃处理权产生的问题通过 issueReported 异步上报，并进入 DrainResult 的错误汇总。

manager 使用上述 Qt 信号。兼容的 `setUpdateCallback` 仍可接收非实时阶段操作在调用线程发出的通知，回调必须迅速返回；它不替代队列事件循环的 updated/完成信号。

采集调用例程仅是协议示意：

```text
tryReserve(captureId, source, inputInfo)
→ memcpy(block.data, input, inputInfo.validBytes)
→ commit(block, inputInfo)
```

下游例程（已在 enabledStages 中启用 Resample）：

```text
tryAcquire(captureId, Resample)
→ 在当前槽位处理 PCM，得到新的格式、长度、帧数
→ commit(block, outputInfo)
```

共享区描述通过只读 QueueInfo 返回：captureId、ipcKey、version、mappedBytes、slotCount、payloadCapacity。当前各工作线程使用 manager 提供的同一队列对象，启用阶段由 enabledStages 明确声明，不引入运行期消费者注册或自动发现。未来跨进程连接沿用这个布局，实际通知后端不作为本轮业务接口参数。

## 满队列、错误和结束标记

普通采集块无法取得槽位时立即返回，不能等待下游或覆盖未释放槽。按来源累计丢失帧数及序号；下一个成功块携带 Gap 和 droppedSourceFramesBefore。接口不承诺所有过载音频最终被消费。

处理失败的块不直接跳过游标或改成 Free，而是保留其 ticket，标记 Dropped、frameCount/validBytes 置零，依次通过剩余阶段，最后释放。剩余启用阶段不计算该负载，但仍取得并确认块，重置该来源必要的连续处理状态；禁用阶段由队列旁路。零输出的正常重采样块没有 Dropped 标记，不能当作错误。

ASR 持有 ReadyForAsr 块时已是最后阶段，其 fail 直接记录错误并释放，不再等待不存在的下一阶段，也不要求对失效的 BlockView 再次 release。

每次暂停的片段边界使用 SegmentEnd；每路最终结束使用 StreamEnd。它们仍占同一队列的普通槽位，带 source/epoch，进入 Captured 后沿同一状态链前进。worker 先回复相应来源已经停稳，manager 再请求 queue 收尾；queue 在所属线程生成、提交尾标并在满队列时异步重试。不能丢掉结束标记，也不能阻塞采集回调。

Drain 时，结束标记起初没有 PCM。转换/重采样器可以在该槽写入有界的尾部 PCM，随后保存阶段先处理尾部音频、再完成编码收尾，ASR 先接收尾部 PCM、再结束对应来源输入，最后释放槽位。标记 kind 不变，即使产生尾部 PCM 也仍表示其后结束。选择的处理器必须支持在一个槽位容量内完成一次收尾；超出上界报错，本版不支持在 flush 时无限扩展输出。

处理失败也必须保留结束标记的 kind，不能使下游遗漏输入结束。采集停止、所有结束标记及数据释放、ASR 文字完成是三个不同事件。

## AudioPiplineManager 方法

```cpp
RequestId refreshDevices();
RequestId checkAvailability(const CaptureConfig& config);
RequestId requestPermissions(AudioSources sources);
bool cancelRequest(const RequestId& requestId);

StartResult startCapture(const CaptureConfig& config);
CommandResult pauseCapture(const CaptureId& captureId);
CommandResult resumeCapture(const CaptureId& captureId);
CommandResult stopCapture(const CaptureId& captureId, StopMode mode);

SharedPcmQueue& pcmQueue();         // C++ 工作线程入口
```

- refreshDevices 更新真实设备列表和能力。checkAvailability 不采样、不弹权限窗口；未知权限返回需要启动时验证，不伪报已就绪。requestPermissions 仅请求平台能够独立申请的权限。环境请求恰有一次成功、失败或取消结果。
- startCapture 受理后创建队列并异步启动来源。sources 中所有批准来源均成功才进入 Running 并发布有效音频；失败回收已启动设备。正式对话降级由上层明确选择后重试，测试可自动选择可用单路。
- pauseCapture 受理后进入 Stopping，立即禁止新音频进入当前片段，停止实际采集；旧片段待处理数据按取消规则结束。持有块的阶段安全退出，处理完带 Cancelled 的 SegmentEnd 后进入 Paused。恢复保持 captureId、设备和队列，epoch 增加，不补录暂停期间声音。
- resumeCapture 受理后进入 Starting，只恢复暂停前正常的来源，恢复成功后进入 Running。运行期一路故障关闭该来源、发布带错误的结束边界，正常来源继续；故障通道在新一轮采集时重试。
- stopCapture 可打断启动、暂停、恢复。Drain 保留已入队音频并发送 StreamEnd；Discard 将待处理音频标记取消，发送带 Cancelled 的 StreamEnd。结束标记仍必须走完，Discard 不要求生成新的尾部 PCM，但需要各模块释放其内部状态及文件资源。

取消期间，不修改持有者正在使用的负载、不解除其映射。所有启用阶段仍需逐块确认，包括 Cancelled 普通音频和尾标；queue 不因为块尚未被 claim 而代替它们 commit/release。持有者返回 commit/fail/release 时，队列识别取消状态，消费其处理权并按取消路径推进，不能发布正常音频。启用阶段收到带 Cancelled 的尾标时必须先丢弃内部缓存、结束片段或来源，再确认该标记；只有禁用阶段可自动旁路。上层仅在 pause 或 stop(Discard) 受理时关闭被取消 epoch 的结果入口；stop(Drain) 继续接收该 captureId 的最终文字，直到 ASR 自己完成。暂停恢复共用 captureId，识别结果还需关联 epoch。

最终停止采集后，启用阶段的处理工作线程继续运行直到 queueDrained。队列不会因一秒或其他期限到达就强制覆盖仍被使用的槽。若启用阶段不再归还块或确认尾标，队列继续等待；该处理模块负责按实际处理时限配置检测并报告 StageStalled，不能伪报完成。没有消费者能够完成 Discard 的情形仅指对应阶段已经显式禁用，不能借此跳过声明启用的处理模块。

仅允许一次活动采集，Paused、Stopping 也占用该次采集；新启动返回 Busy。旧 ID 返回 StaleCapture。同方向重复 pause/resume/stop 不重复派发操作；冲突阶段返回 InvalidState。stop 优先级最高，Drain 可升级为 Discard，不能反向恢复。

## 只读状态

| 对象 | 状态或主要字段 |
| --- | --- |
| environment | 设备列表、能力、权限、刷新状态、错误 |
| capture | captureId、CaptureState、请求来源、正常来源、实际设备、epoch、degraded、各路电平和错误 |
| queueInfo | 共享区域描述；不含 PCM |
| queueSnapshot | QueueState、acceptingSources、stopMode、epoch、占用量、各阶段进度和丢失统计 |

CaptureState：Idle / Starting / Running / Paused / Stopping。首次启动和暂停恢复都使用 Starting，成功后进入 Running；暂停收尾使用 Stopping，旧片段结束后进入 Paused。最终 stop 的 Stopping 包括设备停止、队列收尾和旧共享区关闭；queueDrained 后成功 close 旧队列才回到 Idle。后端停止或共享区关闭失败保持 Stopping、报告错误并允许重试停止，不能假定资源已释放。

QueueState：Closed=0 / Open=1 / Draining=2。Closed 表示尚未创建或已成功关闭；创建后为 Open。acceptingSources 控制各来源的普通采集写入。暂停进入 Draining，acceptingSources=0、stopMode=Discard；queue 确认取消的 SegmentEnd 已通过所有阶段后发送 segmentFinished，manager 将 CaptureState 更新为 Paused；共享区和 Draining 状态保留，直到 resumeEpoch 开始新片段、重新开放正常来源并恢复 Open。最终 stop 也进入 Draining 并清空 acceptingSources，stopMode 区分 Drain 保留已入队音频和 Discard 取消音频。暂停和最终停止期间仍允许启用阶段归还块，并由 queue 自行旁路禁用阶段及提交必要尾标。

Draining 表示封口后收尾，不等于最终关闭；排空是完成事件，不增设队列状态。manager 先收齐停稳回执，再调用 finish；queue 确认各请求来源的 StreamEnd 已通过所有启用阶段的确认和禁用阶段的旁路、全部槽位释放后发送 drained。manager 据此发送对外 queueDrained，并在成功 close 后进入 Closed/Idle；暂时为空不算完成。暂停的 SegmentEnd 只触发 segmentFinished，不触发最终 queueDrained 或 close。尚未发布任何音频就失败的启动通过空 sources 的 finish 异步完成空队列收尾，无须伪造音频结束块。

ChannelState：Disabled / Starting / Running / Paused / Failed / Stopped。degraded 表示批准的来源中有失败通道。静音仍为 Running，音量为零；不把静音当作设备故障。纯电平通知最多 20 次/秒，生命周期和错误立即通知。

## 信号

```cpp
void environmentChanged(const EnvironmentSnapshot& environment);
void requestFinished(const RequestId& id, const RequestResult& result);
void captureChanged(const CaptureSnapshot& capture);
void captureStarted(const CaptureId& id, const CaptureInfo& info);
void captureStopped(const CaptureId& id);
void queueUpdated(const CaptureId& id, quint64 revision);
void queueDrained(const CaptureId& id, const DrainResult& result);
void issueReported(const AudioIssue& issue);
```

| 信号 | 意义 |
| --- | --- |
| environmentChanged | 设备、能力、权限或刷新状态发生变化 |
| requestFinished | 环境请求的唯一终态，包含对应报告或错误 |
| captureChanged | 采集生命周期、通道、实际设备、电平或问题变化 |
| captureStarted | 本次所有批准来源启动成功，info 包含实际设备、源格式及 QueueInfo；恢复不重复发送 |
| captureStopped | 最终停止或启动回滚后，采集回调已停稳，不会再产生普通 PCM；此时队列可能还在处理 |
| queueUpdated | 槽位发布、阶段推进、回收或结束状态变化，提醒所有工作阶段检查队列 |
| queueDrained | 最终停止时，本次所有数据和 StreamEnd 已处理，且没有块被持有；不表示 ASR 文字已生成 |
| issueReported | 包含 captureId、可选 source/ticket、阶段、错误码和说明的问题报告 |

queueUpdated 不携带 PCM，也不意味着每个信号对应一块数据。提交状态和 revision 后才异步唤醒所有相关阶段，不能随机只唤醒一个阶段；等待前后需要检查状态/revision，避免丢失唤醒。下游处理不能通过直接连接在采集回调中同步执行。

创建共享区和安装工作线程连接在正式交付音频之前完成。captureStarted 先于本次首个有效数据通知；captureStopped 先于 queueDrained。后续才关闭共享区，旧标识的通知只能被忽略，不能取得下一次采集的槽位。

最终顺序固定为：captureStopped → queueDrained → 成功 close 旧队列 → captureChanged(Idle)。queueDrained 回调中马上请求下一次 start 仍返回 Busy，调用方等 Idle 后再启动。旧共享区不得因回调重入而被新一轮替换。captureStopped、queueDrained 对同一次最终停止各最多发送一次；释放重试不重复发送。

DrainResult 包含 mode、releasedBlocks、droppedSourceFrames、cancelled 和阶段错误摘要。它描述共享队列的处理结果，不把音频块释放等同于识别成功或文件保存成功；保存错误通过对应块/阶段问题报告。

## 错误和验收边界

AudioError 提供 code、message、source 和 stage；无来源或阶段时使用 None。AudioIssue 附 captureId、AudioError，以及由 hasTicket 标明是否有效的 ticket。错误码覆盖 InvalidArgument、InvalidState、Busy、StaleCapture、StaleBlock、UnsupportedSource、UnsupportedFormat、PermissionDenied、DeviceUnavailable、DeviceDisconnected、QueueFull、BlockTooLarge、ProcessingFailed、StageStalled、BackendStartFailed、BackendStopFailed、SharedMemoryFailed、UnsupportedProtocol。

Full/WouldBlock 是非阻塞取得的状态，不为每次尝试生成一条 UI 错误；溢出数量进入统计并合并报告。原始采集丢失帧数和转换后的帧数分别计量。

验证边界：两个来源不能预留同一槽；未完成写入不会交给下一阶段；旧 ticket 不能提交新槽；禁用阶段显式旁路而启用阶段不得被代领；取消尾标必须得到每个启用阶段的确认；满队列尾标保留并重试；暂停升级及 Drain→Discard 不丢失或重复最终尾标；持有块不被强取；旧 epoch/ID 不产生新会话的完成通知；采集停稳与队列排空通知分离。后续接入算法时还需验证转换长度及平面布局、零输出、连续状态重置和异步 ASR 释放时机。

共享区域和槽位布局参考 ObsTest 的 `shared-memory-queue.c`。春风的音频队列按本文所述的槽位生命周期交付数据。
