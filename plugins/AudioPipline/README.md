# AudioPipline

`audio_pipline` 提供麦克风和系统输出音频采集、设备权限和电平，以及进程内 PCM 交付。采集和转写引擎各自管理生命周期。

## 采集与队列

`AudioPiplineManager` 负责发命令、收回执和汇总会话状态。MIC、SPK 各有一个 `AudioCaptureWorker`，负责设备准备、启动、停止重试、原生回调和电平统计。两个 worker 共用一个设备控制线程，设备枚举、权限以及平台适配封装在采集层。

音频设置页显示当前可枚举且通过只读检查的采集候选，并标记系统默认设备；系统通知设备或默认设备变化时自动刷新。macOS 会排除已断开、无法查询或不符合后端音频流要求的设备。权限、格式及设备是否能够打开仍需在启动采集时最终确认。

页面上的收音测试通过现有采集会话显示麦克风和系统声音电平，不进行转写或保存。测试由用户启动，停止、离开页面或设备发生变化后释放设备；一路启动失败时会尝试继续测试另一路。

启动依次收齐准备、启动和确认回执，发布 `captureStarted` 后再激活采集。命令和回执携带采集 ID 与操作编号，用于过滤过期回执。

用 `CaptureConfig` 选择 MIC、SPK 或两路。设置 `deliverPcm` 后，每路交付独立的 16 kHz 单声道 PCM16LE。设备可以使用其他采样率或声道数，采集后端负责转换。

每个来源写入自己的预分配缓冲，每路最多 128 块，每块最多 320 帧。采集回调以合并后的 Qt 通知唤醒 manager，写入过程与其他来源和读取端独立。缓冲耗尽时丢弃新块，丢帧数随下一个成功交付的块报告。只测电平时 `deliverPcm` 默认关闭。

`pcmReady(id, epoch, source, packet)` 提供自有 PCM、序号、采集时间和前置丢帧数，按来源顺序发出。槽函数应及时返回，可将耗时处理交给自己的工作线程。

`captureStopped` 表示设备已停。`stopCapture(..., Drain)` 在进入 Idle 前交付所有已接收 PCM，最后发出每路的 `pcmEnded`，其中包含剩余丢帧数。转写消费者收到此信号后处理尾音。`Discard` 和暂停会撤销当前片段的待交付 PCM。恢复会创建新缓冲并递增 `epoch`。控制方法应在 manager 所属线程调用。

## 平台

- macOS：麦克风使用 miniaudio；系统输出使用 macOS 14.2 及以上的 Core Audio process tap。目前系统音频后端要求输出设备只有一个输出流，多输出流设备会明确返回不支持。
- Windows：麦克风使用 miniaudio；系统输出使用 WASAPI loopback。
- Linux：麦克风使用 miniaudio；当前系统输出后端返回不支持。

macOS 宿主应用需要提供 `NSMicrophoneUsageDescription` 和 `NSAudioCaptureUsageDescription`。本项目的应用 bundle 已声明两项用途；独立使用此库的其他宿主应自行提供。
