# AudioPipline

`audio_pipline` 提供麦克风和系统输出音频采集，以及一个供处理阶段原地读写 PCM 的共享内存环形队列。采集和转写引擎的生命周期彼此独立；应用音频设置页展示真实设备列表，转写相关页面仍使用演示数据。

## 采集与队列

`AudioPiplineManager` 负责发命令、收回执和汇总会话状态。MIC、SPK 各有一个 `AudioCaptureWorker`，自行完成设备准备、启动、停止重试、原生回调、电平统计和 PCM 分块入队。两个 worker 共用 `captures` 内的 `AudioCaptureContext` 和一个设备控制线程；设备枚举、权限以及平台适配封装在采集层。

音频设置页显示当前可枚举且通过只读检查的采集候选，并标记系统默认设备；系统通知设备或默认设备变化时自动刷新。macOS 会排除已断开、无法查询或不符合后端音频流要求的设备。权限、格式及设备是否能够打开仍需在启动采集时最终确认。

页面上的收音测试通过现有采集会话显示麦克风和系统声音电平，不进行转写或保存。测试由用户启动，停止、离开页面或设备发生变化后释放设备；一路启动失败时会尝试继续测试另一路。

manager 直接持有两个 worker、队列和控制线程，会话状态按用途明确分组。命令和回执携带采集 ID 与操作编号，过期回执不改变当前会话。启动依次收齐准备、启动和确认回执，发布 `captureStarted` 后再发激活命令开放音频写入。停止时收齐 worker 回执，再由队列执行收尾；暂停可以升级为最终停止。

通过 `AudioPiplineManager` 枚举设备、检查权限，并以 `CaptureConfig` 选择 MIC、SPK 或两路来源。`startCapture` 返回受理结果；设备真正启动后发送 `captureStarted`。两路分别转换为 16 kHz、16 位有符号 PCM、单声道后写入队列；块中包含来源、时间和序号，两路不会混音。设备本身可以使用其他采样率或声道数。

`pcmQueue()` 返回本次使用的队列。通过 `CaptureConfig::queue.enabledStages` 显式启用处理阶段，按 `Convert → Resample → Save → Asr` 顺序处理。启用的阶段取得槽位并提交，ASR 不再引用 PCM 后调用 `release`；禁用阶段由队列自动旁路。默认未启用任何处理阶段，因此输入会自动回收。接入消费者时必须启用其阶段，例如 `config.queue.enabledStages = ProcessingStage::Convert | ProcessingStage::Asr`。数据通知 `queueUpdated` 只提示检查队列，不携带 PCM。

队列默认 128 个槽位、每槽 64 KiB。采集回调不等待被占用的队列锁；队列满或锁竞争时丢弃当前输入，累计丢失帧数，并在后续数据块标记缺口。转写和压缩保存算法尚未接入，处理阶段需要由调用方提供。

`SharedPcmQueue` 负责封口、取消、结束标记插入与满队列重试，并在处理完成后发回片段完成或排空通知。`stopCapture(..., Drain)` 保留已入队数据，`Discard` 将其标为取消；两种模式都等待启用的阶段确认结束标记，取消不会抢收持有中的块。启用某阶段却未提供消费者时，暂停和停止会继续等待。

`captureStopped` 表示设备不再写入，`queueDrained` 表示所有块已归还，随后关闭队列并进入 Idle；两者不代表转写文字已生成。manager 和队列生命周期方法应在各自所属 Qt 线程调用，异步状态依赖 Qt 事件循环。

队列使用系统共享内存，但目前队列同步和处理接口仅支持创建进程内的线程；尚未提供跨进程连接或同步接口。

## 平台

- macOS：麦克风使用 miniaudio；系统输出使用 macOS 14.2 及以上的 Core Audio process tap。目前系统音频后端要求输出设备只有一个输出流，多输出流设备会明确返回不支持。
- Windows：麦克风使用 miniaudio；系统输出使用 WASAPI loopback。
- Linux：麦克风使用 miniaudio；当前系统输出后端返回不支持。

macOS 宿主应用需要提供 `NSMicrophoneUsageDescription` 和 `NSAudioCaptureUsageDescription`。本项目的应用 bundle 已声明两项用途；独立使用此库的其他宿主应自行提供。
