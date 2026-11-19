# Transcription test panel

Qt 6.12 静态展示模块，`import Chunfe.AudioTest`，公开组件 `TranscriptionTestPanel`。模块不创建弹窗、计时器、引擎或音频服务，不保存记录，也不触发回应建议。

`TranscriptionTestPanel` 建议内容尺寸 580 × 360，外部弹窗负责标题、关闭及窗口尺寸。上方为 SPK（系统声音）和 MIC（麦克风）的设备与音量条，不显示“等待声音”“有声音”等通道状态文字；下方为时间顺序的转写文本、停止与重新测试按钮。内部状态继续用于异常图标颜色和无障碍描述。

| 输入 | 含义 |
| --- | --- |
| `status` | idle / starting / running / stopped / error |
| `elapsedText` | 本次测试已用时间，默认 00:00 |
| `spkDevice` / `micDevice` | 当前设备名称，长名称中间省略 |
| `spkState` / `micState` | starting / waiting / receiving / error / stopped；waiting 是正常静音 |
| `spkLevel` / `micLevel` | 0 到 1 的独立音量，展示时限制在此范围 |
| `lines` | 完整数组快照：`{id, source: "spk" | "mic", time, text, final}`；id 在一次测试内稳定且唯一，final=false 表示仍在更新 |
| `errorMessage` | 可选异常信息 |

信号：`stopRequested()`、`restartRequested()`。停止按钮在 starting/running 可用；重新测试在 starting 之外可用。停止后保留文本、重测清空文本、关闭停止测试的生命周期均由调用方处理。设备与文本按纯文本展示。内部按稳定 id 更新列表项，查看历史时不会被新文本强制拉到末尾，可点击“回到最新”。

`AudioChannelStatus` 是复用的单通道展示行，输入为 `channel`、`channelName`、`deviceName`、`inputState`、`level`。图标使用 `Chunfe.Ui` 内嵌的官方 IconPark SVG。

CMake target：`transcription_test` / `transcription_testplugin`，仅依赖 Qt Quick / Controls Basic / Layouts 与 `Chunfe.Ui`。
