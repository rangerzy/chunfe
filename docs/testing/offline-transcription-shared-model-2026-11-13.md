# MIC 与 SPK 共用离线模型的验证记录

日期 2026-11-13。测试机器为 Apple M2，8 GiB 统一内存，使用 Qt 6.12.0 的 RelWithDebInfo 构建。模型为 `qwen3-asr-0.6b-q8_0.gguf`，GPU 后端为 Metal。

本文记录采用共享 PCM 队列和独立识别窗口的历史构建。

离线转写引擎加载一份模型并创建一个 GPU 会话。在同一个工作线程上按顺序识别 MIC 与 SPK 的 1 秒窗口，每个窗口最多生成 64 token。应用层为每路分别保留音频余量、断句与文本，普通断句和主动停止都会识别不足一秒的尾段。打包运行库为 audio.cpp 提交 `047fb4243914583e23c41d37f28185f0ebfe1bba`，C ABI 0.3.0。

## 双路负载和预期结果

将两段不同的合成语音按真实节奏同时写入共享 PCM 队列，每路每 200 ms 输入 3,200 帧，格式为 16 kHz 单声道 PCM16LE。长测由 100 轮组成，每轮六秒，两路都在每轮讲话且语音重叠。前 99 轮用静音触发断句。最后一轮分别调整两路语音起始位置，使两路最后一个超过门限的 10 ms 帧都在停止前 200 ms 结束，剩余音频由 `StreamEnd` 冲刷。

| 通道 | 合成文本 | 语音段 | 模型实际尾段 | WAV 的 SHA-256 |
| --- | --- | ---: | ---: | --- |
| MIC | Our red bicycle arrived at the train station, blueberry. | 3.21 s | 0.71 s | `7b8eec2ab71af27a374e1d898e78c3860d2b0530b06525fb90011eb832ee5d34` |
| SPK | The silver rocket circles above the desert, pineapple. | 3.28 s | 0.78 s | `6bf8df31d785117e9b50058e3dd7762353f13b88468666d4dca77b24b03f27b1` |

参考文本由同一个打包运行库直接转写。参考程序一次连续推送一路音频，配置 `audio_chunk_seconds=1.0`、`max_tokens=64`，调用 `stream_finish` 取得完整结果。参考 PCM 与测试中的门控输入一致，含 300 ms 前置静音和最后一次过门限后的 200 ms 尾音。参考程序还分别识别仅含整秒的输入和不足一秒的剩余部分。MIC 的 `Blueberry` 和 SPK 的 `Pineapple` 均由剩余部分识别，只送整秒输入时两个尾词都缺失。

| 通道 | 独立参考文本 |
| --- | --- |
| MIC | Are read by. Michael arrived at the train. Main station. Blueberry. |
| SPK | The silver. Rocket circles above. The dessert. Pineapple. |

参考文本保留模型对短窗的原始识别结果，包括与合成文本不同的用词。每轮逐路检查最终胶囊的来源、数量和文字顺序，比较时忽略大小写、标点和空格。此方法验证相对于独立参考的串路、丢字、丢尾音和断句异常。测试覆盖最终胶囊，并记录首段可见文字的时间。

## 实测结果

12 秒冒烟测试通过。两路各完成一次静音断句和一次 `StreamEnd` 收尾，四个最终胶囊与各自的独立参考一致。首段文字出现在语音开始后 1.22–1.42 秒。发送停止标记后，MIC 和 SPK 分别在 144 ms 和 273 ms 内发布最终胶囊。

600 秒双路长测通过，活动阶段墙钟耗时 600.017 秒。MIC 和 SPK 各完成 100 个最终胶囊，200 个胶囊都匹配各自的独立参考。每句都保留本路尾词，含最后一轮的主动停止。两路无失败或持续积压。原始输出记录一次 Metal 会话初始化。

| 延迟 | MIC | SPK |
| --- | ---: | ---: |
| 首段出字 P50 / P95 / 最大 | 1,207 / 1,230 / 1,266 ms | 1,346 / 1,377 / 1,414 ms |
| 静音断句 P50 / P95 / 最大 | 1,579 / 1,593 / 1,601 ms | 1,669 / 1,683 / 1,716 ms |
| 最后一轮从 `StreamEnd` 到定稿 | 168 ms | 335 ms |
| 待处理音频峰值 | 16,000 字节（500 ms） | 20,160 字节（630 ms） |

进程物理占用加载前为 25–36 MiB，加载后空闲为 1,209 MiB，转写峰值为 **1,287 MiB（1.26 GiB）**，卸载后降至 **57 MiB**。上一次双模型长测峰值为 2,458 MiB，本次少约 48%。两次长测输入不同，此对比反映各自测试的实测峰值。

转写阶段 Metal 动画探针帧提交间隔 P95 为 **24.74 ms**，最长 **56.83 ms**。转写阶段最大 Qt 心跳延迟为 **19 ms**。测试启动的未加载基线另记录一次 678 ms 的 Qt 心跳延迟。独立系统调度探针全程最大延迟 **32.18 ms**。系统交换空间相对基线峰值增长 **162 MiB**，运行中有 warning 内存压力采样，临界内存压力采样为零。观察器正常结束，未触发保护。

应用及测试程序构建成功，常规 CTest **18/18 通过**，耗时 40.13 秒。记录见诊断目录的 `regression-ctest.log`、`long-app.jsonl`、`long-system.jsonl` 和 `long-output.log`。

## 指标口径

- 首段文字延迟从素材开始播放到胶囊首次显示文字。每个 PCM 块在对应的 200 ms 音频时间结束后注入。
- 静音断句延迟从最后一个超过门限的 10 ms 帧结束到胶囊定稿，包含既有的约一秒静音判定。最后一轮另报从发送 `StreamEnd` 到胶囊定稿的延迟。
- 应用测试进程每秒记录 `TASK_VM_INFO.phys_footprint`，同时记录 50 ms Qt 心跳与 Metal 动画窗口的帧提交间隔。推理调用统计包含累积音频和收尾调用。Python 观察器独立记录全系统交换空间、换页、内存压力和调度延迟。
- 语音通过真实队列注入。响应性数据来自 Qt 测试进程、探针窗口和系统调度采样。实体设备采集、完整 App 界面和桌面主观操作体验需要另行测量。系统交换指标覆盖当时运行的所有进程。

## 复现

原始记录位于 `build/diagnostics/offline-asr-shared-2026-11-13/`。保存的 `mic.wav` 和 `spk.wav` 为本次素材，重新合成时语音时长和样本可能随 macOS 版本和语音包变化。以下命令记录素材来源。

```sh
say -v Samantha -r 165 -o mic.aiff 'Our red bicycle arrived at the train station, blueberry.'
say -v Daniel -r 163 -o spk.aiff 'The silver rocket circles above the desert, pineapple.'
afconvert -f WAVE -d LEI16@16000 -c 1 mic.aiff mic.wav
afconvert -f WAVE -d LEI16@16000 -c 1 spk.aiff spk.wav
```

同目录的 `mic-oracle.pcm`、`spk-oracle.pcm` 和 `isolated-oracle.log` 留存原始参考输入和结果，`isolated-oracle.cpp` 留存了临时测试程序源码。参考程序可在仓库根目录编译，按模型路径、MIC PCM 路径和 SPK PCM 路径接收参数。运行前将 `CHUNFE_REAL_ASR_MODEL` 设为本地 GGUF 文件的绝对路径。

```sh
diag="$PWD/build/diagnostics/offline-asr-shared-2026-11-13"
runtime="$PWD/plugins/OfflineTranscription/lib/macos/arm64"
model="${CHUNFE_REAL_ASR_MODEL:?请设置本地 GGUF 文件路径}"
clang++ -std=c++17 -O2 -I plugins/OfflineTranscription/vendor/audio.cpp \
  "$diag/isolated-oracle.cpp" "$runtime/libaudiocpp.dylib" -Wl,-rpath,"$runtime" -o "$diag/isolated-oracle"
"$diag/isolated-oracle" "$model" "$diag/mic-oracle.pcm" "$diag/spk-oracle.pcm"
```

在仓库根目录使用已构建的测试程序复现长测。重复执行前为日志换一个前缀，保留原始结果。

```sh
diag="$PWD/build/diagnostics/offline-asr-shared-2026-11-13"
env -u QT_QPA_PLATFORM -u QT_QUICK_BACKEND -u QTEST_FUNCTION_TIMEOUT \
  PYTHONDONTWRITEBYTECODE=1 \
  CHUNFE_RUN_ASR_SOAK=1 CHUNFE_SOAK_RENDER_PROBE=1 \
  CHUNFE_SOAK_SPEECH_SECONDS=600 CHUNFE_SOAK_IDLE_SECONDS=10 \
  CHUNFE_REAL_ASR_MODEL="${CHUNFE_REAL_ASR_MODEL:?请设置本地 GGUF 文件路径}" \
  CHUNFE_REAL_ASR_WAV="$diag/mic.wav" CHUNFE_REAL_ASR_SPK_WAV="$diag/spk.wav" \
  CHUNFE_SOAK_MIC_EXPECTED='Are read by. Michael arrived at the train. Main station. Blueberry.' \
  CHUNFE_SOAK_SPK_EXPECTED='The silver. Rocket circles above. The dessert. Pineapple.' \
  CHUNFE_SOAK_JSONL="$diag/long-app.jsonl" \
  python3 scripts/diagnostics/offline_transcription_monitor.py \
  --log "$diag/long-system.jsonl" --baseline-seconds 10 --max-swap-growth-mib 3072 --max-seconds 900 \
  -- ./build/relwithdebinfo/src/app/offline_transcription_soak_tests -o -,txt \
  > "$diag/long-output.log" 2>&1
```
