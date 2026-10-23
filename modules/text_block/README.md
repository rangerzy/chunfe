# TextBlock

独立的只读单段落展示组件，以静态 QML 模块提供。模块负责文字布局和绘制，可承载调用方提供的背景；事件、复制和提示由调用方实现。模块只依赖 Qt，不依赖 Chunfe 的应用、日志或主题代码。

- QML URI / 公开组件：`TextBlock` / `TextBlock`
- CMake targets：`text_block`、`text_blockplugin`
- 环境：Qt 6.12+（Core、Gui、Qml、Quick）、C++23、CMake 3.24+

## 接入

在调用 `qt_standard_project_setup(REQUIRES 6.12)` 的 Qt 工程中加入模块并链接两个静态 targets：

```cmake
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
add_subdirectory("/absolute/path/to/modules/text_block" "${CMAKE_CURRENT_BINARY_DIR}/text_block")
target_link_libraries(my_app PRIVATE text_block text_blockplugin)
```

将 `QT_QML_OUTPUT_DIRECTORY` 统一设为构建目录下的 `qml`，可让生成的 QML 类型与工具共用一个导入根目录；若消费者未设置，模块默认使用同一布局。

独立示例的入口显式注册静态 QML 插件：

```cpp
#include <QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(TextBlockPlugin)
```

应用 QML 文件随后可以直接导入：

```qml
import QtQuick
import TextBlock

TextBlock {
    maximumWidth: parent.width
    padding: 16
    font.pixelSize: 18
    textColor: "#24352d"
    background: Rectangle {
        color: "#edf3e8"
        radius: 12
    }
    highlightColor: "#e2eeab"
    content: ({
        text: "先理解问题，再给出清晰的结论。",
        ranges: [
            { start: 9, end: 14, bold: true },
            { start: 7, end: 11, highlight: true }
        ]
    })
}
```

一个实例对应一个段落，多段内容使用多个实例。示例的两段区间在“清晰”处交叠，因此该部分同时显示粗体与高亮。内部 C++ 类型是实现细节，消费者应使用完整的 `TextBlock` 组件。

## 内容和样式

`content` 接受一份完整快照：

```js
{
    text: "完整纯文本",
    ranges: [
        { start: 0, end: 2, bold: true },
        { start: 1, end: 4, highlight: true }
    ]
}
```

`text` 和 `ranges` 应一起替换。流式更新也提交新的完整对象，不原地修改旧对象或内部数组，以便 QML 观察到变化并保持文本与样式对应。

区间使用 UTF-16 的 `[start, end)` 偏移，与 JavaScript 字符串的 `length`、`indexOf` 和 `slice` 使用的单位相同。`end` 不包含在区间内。`bold` 和 `highlight` 是独立的布尔样式，可在同一范围或不同范围中组合；交叠区域同时生效。

无效、越界、空或颠倒的区间会被忽略；拆开字素簇的区间也会被忽略。例如不能把代理对 Emoji、带组合音标的字母、肤色修饰符或 ZWJ 家庭表情切成两半。消费者应在完整字素簇边界生成区间。流式文本本身也应在字素簇边界截断；单纯按 UTF-16 长度切片不适用于任意用户文本。

## 布局和外观

| 属性 | 含义 |
| --- | --- |
| `maximumWidth` | 包含左右内边距的最大总宽度，默认 480 |
| `padding` | 四边内边距，默认 0；极窄时限制到最大宽度的一半 |
| `font` | 基础字体；范围内的粗体与之组合 |
| `textColor` | 文字颜色 |
| `background` | 可选的 QML `Component` 背景插槽，默认 `null` |
| `highlightColor` | 高亮范围的背景颜色 |

短段落的隐式宽度贴合文字与内边距；达到 `maximumWidth` 后自动换行，隐式高度随内容增长。负数或非有限的 `maximumWidth` 按 0 处理。建议绑定 `maximumWidth: parent.width`，并让气泡使用自身隐式尺寸。当前组件根据 `maximumWidth` 进行文本测量；直接设置 `width`、`anchors.fill` 或让布局强制填满宽度，不会改变内部测量上限。

默认不创建背景，也不绘制边框。`background` 可传入 `Rectangle` 或自定义可视组件，模块通过 Loader 创建实例，放在文字后方并铺满组件实际尺寸。背景的隐式尺寸不参与文字测量或组件隐式尺寸计算；圆角、颜色、边框和其他绘制效果由调用方控制。替换背景时旧实例被释放，设为 `null` 后恢复无背景。

背景根节点应使用模块分配的尺寸，不要自行绑定根节点的宽高或位置；自绘背景可根据自身 `width`、`height` 绘制。不需要背景时，仅设置 `content`、字体和最大宽度即可。文字范围的 `highlightColor` 属于文本样式，与整个组件的背景独立。

布局使用 `QTextLayout`，渲染通过 Qt Quick 的 `QSGTextNode`。字体回退、Emoji 外观和测量结果随系统字体与渲染后端变化，应在目标平台实际检查。

默认字体继承应用创建组件时的 `QGuiApplication::font()`，也可以通过 `font` 显式传入。TextBlock 实验室在应用层加载 `resources/fonts/` 的 OPPO Sans / JetBrains Mono，模块不依赖该字体资源 target，也不硬编码品牌字体。

Qt 6.12 的距离场字形节点在创建与回收后可能使用不同的抗锯齿模式。当前 TextBlock 实验室与独立接入示例在创建 `QGuiApplication` 前默认设置 `QSG_DISTANCEFIELD_ANTIALIASING=gray`，让新建节点与 Quick Text 回收节点都使用灰度抗锯齿；已有环境设置会被保留。这是应用级的实验配置，模块本身不修改进程环境。接入其他项目时可按目标显示器选择相同配置。本机 cocoa / Metal / DPR=2 的同背景对照中，两组普通中英文文本在统一灰度后逐像素一致；该结果不代替其他平台与字体的验证。

## 事件和复制

模块不注册点击处理器、快捷键、右键菜单或 ToolTip，不主动获取焦点，也不提供 `copy()` / `copied()`。调用方可以在外层容器或实例中添加自己的处理器，读取自己持有的 `content.text` 完成复制；提示的位置和显示方式同样由调用方决定。模块保留 `Accessible.StaticText` 和文本名称，供辅助技术读取。

TextBlock 实验室与独立示例在调用方接入复制交互，演示右键菜单、系统复制快捷键及复制反馈。这些应用界面依赖 Qt Quick Controls，`text_block` 本身不依赖它。组件不提供编辑、拖动选区或用户修改格式的功能。

## 诊断

只读属性 `layoutCount` 是该实例的布局重建调用次数，`lineCount` 是最近布局的行数，`layoutMicroseconds` 是最近一次文本布局耗时（微秒）。一次事件循环中的多次更新可能合并到一次重建；一次重建内部也可能为解决字形溢出执行多轮排版。

`layoutMicroseconds` 仅测量布局重建，不包括 `content` 解析、场景图更新、GPU 绘制或整帧耗时。它不能用来报告 FPS，也不能单独证明组件比原生 Text 更快。诊断属性用于本地观察；当前示例没有跨平台性能结论。

## 构建与检查

根工程构建最小 Chunfe 应用，示例均为独立工程；根工程与 CI 不构建或测试示例。根目录使用 `-DCHUNFE_BUILD_TESTS=ON` 时，`TEXT_BLOCK_BUILD_TESTS` 默认跟随开启，也可显式覆盖；主应用 GUI 模块的 lint target 为 `chunfe_gui_qmllint`。

[`examples/text_block_lab`](../../examples/text_block_lab) 包含完整实验页面、计时辅助器与调用方交互，QML URI 为 `TextBlockLab`。在仓库根目录独立配置：

```sh
cmake -S examples/text_block_lab -B build/text-block-lab -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/platform" \
  -DTEXT_BLOCK_LAB_BUILD_TESTS=ON -DTEXT_BLOCK_BUILD_TESTS=ON
cmake --build build/text-block-lab
cmake --build build/text-block-lab --target text_block_lab_qmllint text_block_qmllint
ctest --test-dir build/text-block-lab --output-on-failure
./build/text-block-lab/text_block_lab
```

实验室生成普通可执行文件 `text_block_lab`（Windows 为 `text_block_lab.exe`），不生成 `.app`。`TEXT_BLOCK_LAB_BUILD_TESTS` 启用实验室自身的计时、复制和主页面测试；`TEXT_BLOCK_BUILD_TESTS` 启用模块测试，两者默认均为 OFF。实验室不生成根应用的 `application_fonts_tests`。模块与字体资源默认指向仓库路径，分别可用 `TEXT_BLOCK_SOURCE_DIR` 和 `CHUNFE_FONTS_SOURCE_DIR` 覆盖；实验室必需字体资源。

模块测试配置使用 offscreen 平台与软件后端。测试覆盖文字布局、高亮、可选背景和调用方事件接入；目标平台字体及图形后端仍需结合桌面运行验证。系统剪贴板属于应用层的验证范围。

[`examples/text_block_consumer`](../../examples/text_block_consumer) 是独立 CMake 工程，默认使用本仓库的模块目录，也可用 `TEXT_BLOCK_SOURCE_DIR` 覆盖。以下命令在仓库根目录执行：

```sh
cmake -S examples/text_block_consumer -B build/text-block-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/platform" \
  -DTEXT_BLOCK_BUILD_TESTS=OFF
cmake --build build/text-block-consumer
build/text-block-consumer/text_block_consumer
```

示例位于本仓库中时，默认额外接入 `resources/fonts/`，以展示相同的中英文字体。`CHUNFE_FONTS_SOURCE_DIR` 可指定其他位置的该资源目录；传入 `-DCHUNFE_FONTS_SOURCE_DIR=` 可关闭品牌字体，验证仅依赖 TextBlock 与 Qt 的接入。将示例移出仓库且未指定字体目录时，默认使用应用的系统字体。

示例还提供截图后退出的检查入口；该入口仅存在于独立示例，不属于模块 API：

```sh
build/text-block-consumer/text_block_consumer \
  --screenshot /tmp/chunfe-textblock-consumer.png
```

Windows 下可选本机可用的构建目录，使用对应 Qt / MinGW 环境运行 `text_block_consumer.exe`；不要照搬示例中的 Unix 路径。
