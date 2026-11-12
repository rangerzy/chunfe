# Dialogue transcript

Qt 6.12 静态 QML 模块，`import Chunfe.Transcript`，组件 `TranscriptPanel`。

输入：`messages` 为完整数组快照（`{role: "other" | "me", text, time}`），另有 `paused`、`collapsed`、`elapsedText`、`statusText`、`logoSource`。建议宽度 480，展开隐式高度 368，折叠高度 64。

信号：`pauseRequested()`、`collapseRequested()`、`finishRequested()`、`dragRequested()`。调用方处理状态与窗口移动；本模块不创建窗口、定时器或音频服务。滚动列表使用 TextBlock；浏览旧内容时不随新消息强制滚动，可点击“回到最新”。

依赖 Qt Quick/Controls Basic、`Chunfe.Ui` 和 `TextBlock`；CMake target 为 `interview_transcript` / `interview_transcriptplugin`。
