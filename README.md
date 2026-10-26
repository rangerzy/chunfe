# 春风 · Chunfe

**遇事不决，可问春风。**

[GitHub 仓库](https://github.com/rangerzy/chunfe) · [构建与发布](https://github.com/rangerzy/chunfe/actions/workflows/build.yml) · [历史发布](https://github.com/rangerzy/chunfe/releases)

春风是一个基于 **Qt Quick** 的桌面端 AI 面试辅助工具项目，帮助面试者理解面试官的问题、组织回答思路，并获得参考回答。

## 当前阶段

当前版本：`0.1.0`。根工程构建最小 **Chunfe 应用壳**：`src/app/main.cpp` 完成启动与字体初始化，`src/gui/Main.qml` 显示应用窗口。面试问题识别、AI 回答建议与面试上下文处理尚未实现。

`src/app`、`src/core`、`src/gui` 各自拥有 `CMakeLists.txt`，由 `src/CMakeLists.txt` 按 `core → gui → app` 引入，共享根工程的 `project(Chunfe)` 与配置。`app` 管理可执行目标、启动和安装部署；`gui` 管理静态 QML 模块 `chunfe_gui`（URI 为 `Chunfe`），可放置 QML 页面及其 C++ 适配代码；`core` 预留给应用业务逻辑，目前只有构建入口和职责说明，不创建空库或占位类。通用可复用组件继续放在 `modules/`。`examples/` 中的实验室和接入示例均为独立工程，根工程不包含示例，也不提供构建示例的开关；CI 不构建或测试示例。

[`examples/text_block_lab`](examples/text_block_lab) 是 TextBlock 文本气泡实验室，用本地静态样本观察只读单段落气泡的自然宽度、换行、粗体与高亮、混合文字、流式更新和多个独立实例。以下实验说明仅适用于实验室。

实验页面提供宽度调节、流式开始／暂停／重置、复制，以及 6／30／100 个实例切换。静态对照的 TextBlock 与原生 Qt Quick `Text` 使用相同纯文本、基础字体、有效最大宽度、背景色、圆角和内边距；原生 Text 显式使用 `QtRendering`。布局活动观察与性能测量分开运行。

### 应用字体

默认字体候选列表为 **Inter → OPPO Sans 4.0**，并将 OPPO 显式注册为 Han 脚本回退。英文和 ASCII 数字优先使用 **Inter 4.1**，中文使用 **OPPO Sans 4.0**。随应用内嵌 Inter 与 Inter Display 的全部 36 个静态 TTF：两个系列各包含 100–900 的九个字重及对应斜体。正文使用 Regular（400），标题和重点可继续请求半粗体（600）和粗体（700）；需要展示字体的组件可显式选择 `Inter Display`。Emoji 沿用平台字体回退。

保留原始字体数据，按字形覆盖范围选择符号与标点的字体，不按全角／半角逐字符强制分配。Inter 未覆盖的字形继续回退；不修改或子集化字体。Inter 的变量字体、TTC、OTF 和 Web 格式保留为来源文件，不打包到应用。

原始字体、许可证、版本来源和加载代码统一放在 [`resources/fonts/`](resources/fonts/README.md)，通过独立 `chunfe_fonts` target 内嵌到主应用及实验室，在 QML 创建前加载，无需用户安装字体或联网。界面控件、自绘 TextBlock、原生 Text 对照与性能测试使用同一套应用字体配置；系统绘制的窗口标题栏由操作系统控制。实验页面底部的“字体许可”可阅读两份原始许可证。`text_block` 模块本身不依赖这些字体，其他项目仍可通过 `font` 传入自己的配置。

### 布局观察

“布局观察”默认开启。TextBlock 显示**布局重建次数**；原生 Text 显示 `lineLaidOut` 中首行触发的**首行布局回调次数（观测）**。两者口径不同，不能把数字直接当成同一种内部布局次数。共同输入更新次数按文本快照或有效宽度变化记录；“重置统计”使用当前计数作为新基线，重新开启观察也会重置基线。

TextBlock 的“最近布局”微秒数仅覆盖文本布局，不含内容解析、场景图提交、绘制或帧率。布局回调观察本身可能增加开销，因此性能模式会断开原生 Text 的观察连接。

### 同负载性能测试

选择 6／30／100 个实例后开始测试，本轮实例数随即固定。程序暂停流式更新、隐藏普通实验页面，使用固定 **640 × 320** 视口，仅加载当前被测组件的实例，并将全部实例交叠摆放在视口内。它测量固定视口下的一组同时更新；交叠场景的结果不能直接等同于真实聊天列表的 GPU 负载。

先运行 TextBlock，再运行 Qt Quick Text。两组使用同一份预生成的纯文本快照和宽度序列，每批同时改变内容与宽度；各进行 **5 批预热、30 批正式测量**，预热不计入结果。表格报告每项的中位数、P95（最近秩法）以及有效样本／已完成正式批次数。可随时停止，保留已完成的部分结果。

| 测量项 | 范围 |
| --- | --- |
| 属性更新 + polish（μs） | 属性传播、整棵样本树的 polish 与几何稳定检查；包含测试辅助代码的开销 |
| 更新至帧提交（ms） | 从修改属性前到后续同一帧的 `afterFrameEnd`，包含调度等待；不是 GPU 完成时间或屏幕呈现时间 |
| 帧同步至渲染记录（ms） | 整个窗口从 `beforeSynchronizing` 到 `afterRendering` 的墙钟区间，包含窗口内的其他工作 |

辅助器最多执行 8 次 polish 树遍历；若仍观察到几何变化，该批标为未稳定。未稳定或数值无效的样本保留在完成批数中，但不参与中位数和 P95。这里的稳定检查不代表 Qt 内部布局轮数。这些测量用于本机同负载比较，不能直接推出 FPS、GPU 耗时或跨平台性能结论。

## TextBlock 模块

`modules/text_block` 是独立的静态 QML 模块，URI 和公开组件名均为 `TextBlock`，CMake target 为 `text_block`。一个实例承载一个段落：短文本贴合内容，达到包含内边距的 `maximumWidth` 后换行并增加高度。

```qml
import QtQuick
import TextBlock

TextBlock {
    maximumWidth: 480
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

- `content` 是 `{ text, ranges }` 的完整快照；更新时一起替换文本与样式区间，不原地修改内部数组。
- 区间使用 UTF-16 的 `[start, end)` 偏移；粗体和高亮可以重叠。无效区间会被忽略，区间边界不能拆开组合字符或 Emoji 字素簇。
- `font`、`textColor`、`highlightColor` 控制文字样式；`padding` 默认 0。尺寸由组件的隐式宽高确定，`maximumWidth` 是包含内边距的总宽度上限。
- `background` 是可选组件插槽，默认 `null`，不创建背景。调用方定义颜色、圆角、边框或自绘效果，模块将背景铺在文字下方；背景的隐式尺寸不影响文字布局。
- 点击、焦点、快捷键、复制、菜单和 ToolTip 由调用方提供。实验页面与独立示例演示这些操作，`text_block` 本身只展示文字，不提供编辑和拖动选区。
- `layoutCount`、`layoutMicroseconds`、`lineCount` 是只读诊断属性，用于本地实验。

完整实验室位于 [`examples/text_block_lab`](examples/text_block_lab)。它与接入示例 [`examples/text_block_consumer`](examples/text_block_consumer) 均单独配置和构建，接入命令与接口说明见 [`modules/text_block/README.md`](modules/text_block/README.md)。

## Calendar 模块

[`modules/calendar`](modules/calendar/README.md) 是从 `examples/calendar_before_today` 提取的静态 QML 模块，通过 `import Calendar` 使用 `PastDateCalendar`。支持起始年月到今天的日期选择、年月导航、液态选中动画、日期数据圆点及选择／月份变化通知，不依赖主应用或字体资源。

独立接入示例 [`examples/calendar_consumer`](examples/calendar_consumer/README.md) 展示日历和所选日期，可单独配置、构建及运行；原 calendar 示例保留为原型参考。

## 开发环境

- Qt **6.12+**，主应用与示例需要 Qt Quick 和 Qt Quick Controls；独立 `text_block` 模块只需要 Qt Quick 及其基础库。macOS / Windows CI 固定使用 `6.12.0`。
- 支持 **C++23** 的编译器。
- CMake **3.24+**、Ninja。
- macOS 使用 Apple Clang；Windows 使用 Qt 配套的 MinGW x64；Linux Flatpak 使用 KDE SDK 中的编译器。

通过 `CMAKE_PREFIX_PATH` 指定 Qt 安装路径，机器专属路径不提交到仓库。

### macOS ARM64

```sh
cmake --preset macos-arm64-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.12.0/macos"
cmake --build --preset macos-arm64-release
./build/macos-arm64-release/Chunfe.app/Contents/MacOS/Chunfe
```

该预设面向 Apple Silicon，最低部署目标为 **14.4**。主应用生成 `Chunfe.app` bundle。

### Windows x64

在已配置 Qt 6.12 及其配套 MinGW 的 PowerShell 中执行：

```powershell
cmake --preset windows-x64-release "-DCMAKE_PREFIX_PATH=$env:QTDIR"
cmake --build --preset windows-x64-release
.\build\windows-x64-release\Chunfe.exe
```

`QTDIR` 指向 Qt 的 MinGW 安装目录，Qt 和匹配 MinGW 的 `bin` 目录需位于 `PATH`。CI 使用 Qt 6.12.0 配套的 MinGW 13.1.0。

### Linux

本地已安装 Qt 6.12 开发环境时：

```sh
cmake --preset linux-x64-release -DCMAKE_PREFIX_PATH="$QTDIR"
cmake --build --preset linux-x64-release
./build/linux-x64-release/Chunfe
```

主应用使用 Flatpak 打包。该配置在 KDE SDK 内构建，沿用 `jian` 的 **Flathub Beta `6.12-beta`** 运行时，允许 Qt 6.12 补丁更新；构建时会检查 SDK 的 Qt 版本。构建及安装步骤见 [Linux Flatpak 说明](packaging/linux/README.md)。

### Debug 与检查

```sh
cmake --preset debug \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/platform" \
  -DCHUNFE_BUILD_TESTS=ON
cmake --build --preset debug
cmake --build --preset debug --target chunfe_gui_qmllint text_block_qmllint calendar_qmllint
ctest --test-dir build/debug --output-on-failure
python3 -B -m unittest discover -s tests -v
```

`CHUNFE_BUILD_TESTS=ON` 启用根应用的 `application_fonts_tests`；`TEXT_BLOCK_BUILD_TESTS` 与 `CALENDAR_BUILD_TESTS` 首次配置时默认跟随该开关，也可显式覆盖。模块测试为 `text_block_tests`、`text_block_layout_tests` 和 `calendar_tests`。Qt Test 使用 offscreen 平台与软件后端；字体测试检查实际字形使用的字体及字重，覆盖相邻中英文、数字、标点、粗体和 QML 控件继承。Python 测试覆盖 Release 发布逻辑，无需网络或 GitHub 凭据。

实验室需单独配置；以下命令同时启用实验室与模块测试：

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

实验室拥有 `experiment_metrics_tests`、`text_copy_actions_tests` 和 `main_qml_tests`，不生成根应用的 `application_fonts_tests`。这些测试检查实验计时、复制交互、编辑器模块元数据，以及窗口宽度、气泡宽度、段落数量和流式内容变化时的布局与运行警告。系统剪贴板与实际绘制仍需桌面验证。

两个示例默认使用仓库中的模块路径，可通过 `TEXT_BLOCK_SOURCE_DIR` 与 `CHUNFE_FONTS_SOURCE_DIR` 指定其他位置。实验室必需字体资源；consumer 可将字体路径设为空，使用系统字体。独立接入命令见 [模块说明](modules/text_block/README.md)。

Qt Creator 的代码模型通过 CMake 缓存中的 `QML_IMPORT_PATH` 查找构建目录下的模块；工程已自动加入 `build/.../qml`。首次配置后需构建一次，生成 `qmldir` 和 `.qmltypes`。`TextBlock.qml` 和 `examples/text_block_lab/Main.qml` 保留各自模块的显式导入，兼容 Qt Creator 对源码文件中 C++ 组件的识别。Qt 6.12 的 qmlls 元数据生成依赖也已补齐，避免增量配置继续使用旧路径。若编辑器保留旧诊断，可在构建后执行“工具 → QML/JS → 重置代码模型”。

## 应用安装包

根工程已提供 `Chunfe` 目标及安装规则。macOS 安装时部署 bundle 的 Qt / QML 依赖；Windows 打包以 `src/` 为 `--qmldir` 扫描目录；Linux 安装可执行文件、desktop 文件和 SVG 图标。以下是现有打包入口，跨平台结果仍需运行相应脚本或 `main` 分支 CI 验证。

| 平台 | 发布文件 | 内容 |
| --- | --- | --- |
| macOS ARM64 | `chunfe-0.1.0-macos-arm64.dmg` | 含 Qt / QML 依赖的 `Chunfe.app` |
| Windows x64 | `chunfe-0.1.0-windows-x64-setup.exe` | Inno Setup 安装包，含 Qt / QML 和 MinGW 运行库 |
| Linux x64 | `chunfe-0.1.0-linux-x64.flatpak` | Flatpak 单文件应用包，依赖 KDE `6.12-beta` 运行时 |

本地打包入口：

```sh
# macOS：先完成 macos-arm64-release 构建
bash packaging/macos/package.sh

# Windows PowerShell：先完成 windows-x64-release 构建，安装 Inno Setup 6.3+
./scripts/ci/package-windows.ps1

# Linux：依赖与安装步骤见 packaging/linux/README.md
bash scripts/ci/build-flatpak.sh
```

macOS 打包会部署 Qt / QML、进行 ad hoc 签名并检查 DMG；Windows 会检查依赖并验证静默安装后的文件；Linux 会检查 Flatpak 的构建、导出和安装内容。这些流程尚未执行应用启动测试。

macOS 尚未配置开发者证书签名或 Apple 公证，Windows 尚未配置代码签名证书。Linux 使用独立 Flatpak 包分发，尚未提交到 Flathub 应用目录。

## 分支与自动发布

日常在 **`dev`** 开发，准备发布时手动递增 `cmake/AppVersion.cmake` 中的
`VERSION_MAJOR`、`VERSION_MINOR`、`VERSION_PATCH` 或 `VERSION_BUILD`。
四段组成唯一公开版本 `APP_VERSION`，例如 `0.1.0.0 → 0.1.0.1`。
每段使用无前导零的十进制整数，范围为 `0..65535`。
根 `CMakeLists.txt` 引入这份定义，应用版本、标签和三个安装包文件名均使用完整四段版本。
本地仍可用 CMake `-DVERSION_BUILD=...` 等参数覆盖版本；正式发布必须与提交中的版本及标签一致。

```sh
# 在 dev 修改代码和 cmake/AppVersion.cmake，然后验证、提交并推送
python3 scripts/ci/app_version.py
python3 -B -m unittest discover -s tests -p 'test_*.py'
git push origin dev
# 通过 dev → main 的 Pull Request 合并发布变更
```

合并或推送到 `main` 后，流程分两步：

1. `Prepare version release` 比较这次推送前后的声明版本。版本未变则结束；
   版本变更且未被使用时，为该提交创建 `v<APP_VERSION>` 标签，例如 `v0.1.0.1`。
   已有标签指向其他提交、版本倒退或 API 校验失败都会报错，不会强制移动标签。
2. 标签创建后，用 GitHub 内置凭据显式调用 `Build and release`，运行目标为刚创建的标签。
   工作流校验标签与源码版本一致、提交属于 `main` 历史、正式应用入口存在，
   再构建 macOS ARM64 DMG、Windows x64 安装器和 Linux x64 Flatpak。
   三平台全部通过后创建草稿 Release，上传并校验三个附件，最后公开发布。

GitHub 内置 `GITHUB_TOKEN` 创建标签不会自动触发另一个 `push` 工作流，
所以自动流程使用 `workflow_dispatch(ref=标签)`。这仍然是自动发布：无须点击 Run workflow，
也无须额外配置 PAT、GitHub App 或 Secret。标签准备任务需要 `contents: write` 和
`actions: write`；构建任务只有 `contents: read`；发布任务使用 `contents: write`。
仓库或组织若限制这些权限，需要管理员允许相应内置凭据权限。

`Build and release` 同时支持人工推送 `v*` 标签和手动选择已有版本标签运行。
手动选择分支运行会在入口校验失败；标签必须精确匹配四段声明版本且对应 `main` 历史中的提交。
推送 `dev` 或更新 PR 不会触发正式构建/发布；CI 不构建或测试 `examples/`。

**版本与标签发布后保持不变。** 发布脚本不会创建或移动标签，也不会更新已经公开的 Release、
替换其附件或重新修改 Latest。新代码需要新版本。失败时可使用以下恢复方式：

- 自动打标签成功、启动构建失败：重跑 `Prepare version release`，它确认标签仍对应原提交后继续启动构建。
- 构建或发布失败：在原运行中选择重跑失败的任务，沿用原标签和提交，不递增版本。
- 草稿上传中断：重跑后保留校验通过的附件，只补充或替换草稿中的缺失/损坏附件。
- 版本已经公开：再次执行发布会直接跳过，保持现有版本内容不变。

发布说明记录四段版本、提交和工作流链接，不强制比较重写前的旧提交历史。
旧 `v0.1.0` Release 与标签保留不变；首次合并这套配置时，旧三段 `0.1.0`
可迁移为独立四段 `0.1.0.0`，生成新标签 `v0.1.0.0`。新版本按四个数值分段比较，
较旧版本的重试不会把 Latest 从较新版本移回去。发布任务使用仓库统一队列串行执行，
最多排队 100 个任务，防止多个版本同时修改 Latest；三平台构建仍可并行。

在 Actions 中确认自动准备任务成功，再确认构建运行标题含 `workflow_dispatch`、版本标签和
对应提交 SHA；三平台任务与发布任务都成功后，Release 应包含三个同版本安装包。
这些配置和本地测试不代表已经完成远程三平台构建验证。

macOS 的应用包短版本使用前三段，构建版本使用 `主.次.(补丁×65536+构建)`，
以满足平台的一至三段元数据格式；公开标签、应用内版本和安装包文件名仍保持四段。
Windows 安装器版本使用完整四段；Flatpak 的 `stable` 分支与应用版本相互独立。

GitHub 远端为 `https://github.com/rangerzy/chunfe.git`，默认分支为 `main`。
本地 `main`、`dev` 分别跟踪 `origin/main`、`origin/dev`。

## 工程结构

```text
CMakeLists.txt             # 项目、主应用、共享模块与测试开关
cmake/AppVersion.cmake     # 手动维护的四段版本唯一来源
CMakePresets.json          # 三平台 Release 与 Debug 构建预设
src/CMakeLists.txt        # 统一引入 core、gui、app 三个内部子目录
src/app/                  # 独立 CMake 管理 Chunfe 目标、启动、组装与安装部署
src/gui/                  # 独立 CMake 管理 chunfe_gui 静态 QML 模块及界面适配
src/core/                 # 业务层构建入口及职责说明，实际源码加入后再建库
modules/text_block/       # 可复用静态 QML 模块与模块 Qt Test
modules/calendar/         # 可复用日历模块与日期交互、液滴动画测试
resources/fonts/          # 原始字体、许可证及共享字体加载
examples/text_block_lab/  # 独立实验室与自身测试
examples/text_block_consumer/ # 独立接入示例
examples/calendar_before_today/ # 原日历原型
examples/calendar_consumer/ # Calendar 模块独立接入示例
packaging/                # 主应用 DMG、Inno Setup、Flatpak 配置
scripts/ci/               # Windows / Linux 打包脚本
.github/workflows/        # main 检测版本并打标签，按标签构建与发布
.github/scripts/          # Release 发布脚本
tests/                    # 根应用字体 Qt Test 与 Release 离线测试
```
