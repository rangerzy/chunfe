# Answer panel

Qt 6.12 静态 QML 模块，`import Chunfe.Answer`，组件 `AnswerPanel`。

输入：`question`（当前建议对应的对话片段，兼容原字段名）、`triggerKind`（question / request / objection，显示问题/请求/异议）、`answer`（首次回应建议的当前文本快照）、`generating`、`pendingQuestion`（正在准备建议的新片段，旧建议仍可读）、`collapsed`。新建议开始输出时，调用方同时更新 `question` / `triggerKind` / `answer` 并清空 `pendingQuestion`。普通陈述不需要更新此模块。

建议宽度 400，展开隐式高度 460，折叠高度 54。按空行分段使用 TextBlock 呈现，首段强调，长内容滚动。信号为 `collapseRequested()` 和 `dragRequested()`；窗口与生成生命周期由应用管理。不含模型调用、固定、队列、补题、重生成或代码示例。

依赖 Qt Quick/Controls Basic、`Chunfe.Ui` 和 `TextBlock`。CMake target 为 `answer_panel` / `answer_panelplugin`。
