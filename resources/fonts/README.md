# 应用字体资源

本目录统一管理应用字体、原始许可证和启动加载代码。`chunfe_fonts` 是仅依赖 Qt Gui 的共享静态资源 target；当前由 [`examples/text_block_lab`](../../examples/text_block_lab) 实验室和启用项目字体的独立接入示例使用，后续正式应用也可接入。`modules/text_block` 不依赖它，消费者可自行选择字体。

## 字体与来源

| 文件 | 版本 / 字重 | 用途与来源 |
| --- | --- | --- |
| `oppo-sans/OPPO Sans 4.0.ttf` | OPPO Sans 4.0，可变字体 | 中文及常用中文标点；用户提供的 `OPPO_Sans_4.0.zip` 原始文件 |
| `jetbrains-mono/JetBrainsMono-Regular.ttf` | JetBrains Mono 2.304 / 400 | 正常英文、数字及半角标点 |
| `jetbrains-mono/JetBrainsMono-SemiBold.ttf` | JetBrains Mono 2.304 / 600 | 标题等现有半粗体样式 |
| `jetbrains-mono/JetBrainsMono-Bold.ttf` | JetBrains Mono 2.304 / 700 | 重点加粗样式 |

JetBrains Mono 来自 [JetBrains 官方 2.304 发布](https://github.com/JetBrains/JetBrainsMono/releases/tag/v2.304)的 [JetBrainsMono-2.304.zip](https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip)，使用其中 `fonts/ttf/` 的三个静态字体。OPPO 字体与两份许可证均从对应压缩包原样复制；没有修改或子集化字体。

正文默认字重为 400。OPPO 可变字体保留完整字重能力，标题和重点继续按现有样式请求更高字重。加载器不设置 `Regular` styleName，也不固定变量轴，以免覆盖加粗请求。连字遵循字体及 Qt 的默认行为。

字体选择遵循原版字体的字形覆盖范围：英文、ASCII 数字及半角标点优先使用 JetBrains Mono，中文及 `，。！？` 等常用中文标点回退到 OPPO Sans。JetBrains Mono 已包含的 `｛｝`、智能引号、省略号等符号仍使用 JetBrains Mono。这是已确认的例外规则，不按全角／半角逐字符强制指定字体，也不生成派生字体。Emoji 保留平台回退。

## 接入

在已有 Qt 工程中添加并链接资源 target：

```cmake
add_subdirectory(resources/fonts)
target_link_libraries(my_app PRIVATE chunfe_fonts)
```

创建 `QGuiApplication` 后、加载 QML 之前，在 GUI 线程调用：

```cpp
#include "app_fonts.h"

QGuiApplication app(argc, argv);
if (!ChunfeFonts::initialize())
    return 1;
QQmlApplicationEngine engine;
```

加载器保留应用原有字号，配置字体候选列表 `JetBrains Mono → OPPO Sans`，并注册 OPPO 为 Han 脚本的应用回退字体。实际 family 名从 `QFontDatabase::applicationFontFamilies()` 获取。字号和字重仍可通过组件的 `font` 属性调整；不要在各处重设 `font.family`，以免覆盖统一候选列表。

成功初始化后，重复调用不会重复注册字体。资源通过 `Q_INIT_RESOURCE(chunfe_font_assets)` 显式链接，路径以 `:/fonts/` 开头，不依赖工作目录。加载失败时打印具体资源路径、撤销本次已注册字体并返回 `false`。

## 许可证与声明

应用使用 **OPPO Sans 字体**。版权所有 © 2024 Guangdong OPPO Mobile Telecommunications Corp., Ltd. 原始许可位于 [`oppo-sans/OPPO Sans 4.0 License Notice.txt`](<oppo-sans/OPPO Sans 4.0 License Notice.txt>)；它要求在软件中显著声明使用 OPPO Sans，保留许可和版权声明，并随软件分发未修改的字体。TextBlock 实验室与启用项目字体的独立示例通过“字体许可”入口展示完整许可；初始化后的 `ChunfeFonts::licenseNotices()` 提供带标题的两份原文。该字体不能作为独立字体产品再分发。

JetBrains Mono 的字体许可为 **SIL Open Font License 1.1**，原文位于 [`jetbrains-mono/OFL.txt`](jetbrains-mono/OFL.txt)。

两份原始许可证同时打包到 Qt 资源：

- `:/fonts/oppo-sans/OPPO Sans 4.0 License Notice.txt`
- `:/fonts/jetbrains-mono/OFL.txt`

## SHA-256

用于校验来源压缩包与本目录的原始文件；压缩包本身不重复保存到仓库。

| 文件 | SHA-256 |
| --- | --- |
| `OPPO_Sans_4.0.zip` | `412d10fd4ed8447e31eec913a9bd4814816ce9614c12eaae23f77dcb0f54f97d` |
| `JetBrainsMono-2.304.zip` | `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf` |
| `oppo-sans/OPPO Sans 4.0.ttf` | `6c7d5864c661516e1f400d9f21e4297f2e2a0719909691e29607cc4ef484a9f4` |
| `oppo-sans/OPPO Sans 4.0 License Notice.txt` | `b013f80fa2fa6f6db4a31b793676f787af78263b568088a2d896e9d9b3e5fde7` |
| `jetbrains-mono/JetBrainsMono-Regular.ttf` | `a0bf60ef0f83c5ed4d7a75d45838548b1f6873372dfac88f71804491898d138f` |
| `jetbrains-mono/JetBrainsMono-SemiBold.ttf` | `1b3bfa1ed5665a4ce3f9feb68d2d4e40e70bf8b4b7d9a3edd418f321b4e166a0` |
| `jetbrains-mono/JetBrainsMono-Bold.ttf` | `5590990c82e097397517f275f430af4546e1c45cff408bde4255dad142479dcb` |
| `jetbrains-mono/OFL.txt` | `30f0c136e3c88e422d0791acd97238870f9054a9729bc34cf2ff0d4ed8cac4ad` |
