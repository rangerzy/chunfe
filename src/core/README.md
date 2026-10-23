# 应用核心

此目录承载 Chunfe 自身的业务逻辑、数据模型和服务。当前仅建立最小应用窗口，尚无业务实现。

- 可以使用 Qt Core、Qt Network 等基础能力。
- 不依赖 QML 页面、窗口或 `src/gui`、`src/app`。
- 面向 QML 的界面适配与展示状态放在 `src/gui`；应用初始化与服务组装放在 `src/app`。
- 通用可复用组件继续放在仓库的 `modules`，不因使用 C++ 就迁入这里。

本目录的 `CMakeLists.txt` 由根工程经 `src/CMakeLists.txt` 引入，目前不生成构建 target。有实际业务代码后再定义 `chunfe_core` 库，避免空库和占位类。
