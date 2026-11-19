# 转写测试实施计划

规格：`../specs/2026-11-03-transcription-test.md`，用户已确认。

- [x] 独立Qt展示模块与mock状态控制器。
- [x] Qt设置入口、弹窗宿主与生命周期集成。
- [x] 网页独立模块、入口、引擎共享与弹窗清理。
- [x] 状态与窗口测试、构建/lint、浏览器验证、截图及启动预览。

分工：audio_test_ui_module负责新modules/transcription_test；audio_test_native_state负责mock控制器/DemoController/定向状态测试；audio_test_web负责网页；根线程负责Qt宿主与集成、文档和最终验证。

验证结果：

- Qt 6.12 debug 构建与 GUI、测试转写、通用控件、字幕和建议模块的 qmllint 通过；ctest 5/5 组通过。最终控制器调整后再次构建、lint 和运行 native_ui_tests，通过。
- 原生新增状态测试和弹窗交互测试，覆盖启动/停止/重测/关闭、迟到启动回调、单路异常、40条上限、正式对话与记录隔离，以及820×560/760×520下的布局。
- 隔离浏览器29项检查通过，无脚本异常；另有19项独立网页控制器检查通过。验证两路动态电平、来源/时间、滚动保持、启动竞态、记录隔离和正式对话回归。
- 截图保存在 `build/debug/audio-test-previews/`；网页检查报告在其 `web/report.json`。Qt新版已启动，网页使用 `?v=20261103-9#engine`。
