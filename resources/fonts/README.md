# 应用字体资源

本目录统一管理应用字体、原始许可证和启动加载代码。`chunfe_fonts` 是仅依赖 Qt Gui 的共享静态资源 target；由根工程的 Chunfe 主应用、独立 [`examples/text_block_lab`](../../examples/text_block_lab) 实验室和启用项目字体的独立接入示例使用。`modules/text_block` 不依赖它，消费者可自行选择字体。

## 字体与来源

| 文件 | 版本 / 字重 | 用途与来源 |
| --- | --- | --- |
| `oppo-sans/OPPO Sans 4.0.ttf` | OPPO Sans 4.0，可变字体 | 中文及常用中文标点；用户提供的 `OPPO_Sans_4.0.zip` 原始文件 |
| `Inter-4.1/extras/ttf/Inter-*.ttf` | Inter 4.1 / 100–900，正常与斜体，共 18 个静态 TTF | 默认英文、数字及已覆盖符号；用户提供的 Inter 4.1 原始分发目录 |
| `Inter-4.1/extras/ttf/InterDisplay-*.ttf` | Inter Display 4.1 / 100–900，正常与斜体，共 18 个静态 TTF | 显式选择 `Inter Display` 时使用；同一原始分发目录 |

应用内嵌并注册 `Inter-4.1/extras/ttf/` 下的全部 36 个静态 TTF。两个系列均包含 Thin（100）、ExtraLight（200）、Light（300）、Regular（400）、Medium（500）、SemiBold（600）、Bold（700）、ExtraBold（800）、Black（900），每个字重都有正常与斜体样式；400 的斜体文件名为 `Inter-Italic.ttf` 或 `InterDisplay-Italic.ttf`。格式说明见原始 [`Inter-4.1/help.txt`](Inter-4.1/help.txt)。

`InterVariable.ttf`、`InterVariable-Italic.ttf`、`Inter.ttc`、`extras/otf/` 和 Web 字体等其他格式保留在来源目录，不打包到 Qt 资源。所有字体及两份许可证保留原始内容，没有修改、转换或子集化。

正文默认字重为 400。OPPO 可变字体保留完整字重能力，Inter 与 Inter Display 可通过 `font.weight` 和 `font.italic` 请求字重和斜体。标题和重点继续按现有样式请求更高字重。数值字重的实际匹配由平台决定：本机 Qt 6.12 / macOS CoreText 下，ExtraBold（800）请求可能选中 Black（900）。需要精确指定字体样式时，可配合 `Inter` 或 `Inter Display` family 使用 `font.styleName: "ExtraBold"` 或 `font.styleName: "ExtraBold Italic"`。显式 `styleName` 会覆盖字重和斜体选择，因此加载器不全局固定 `styleName`，也不固定变量轴。连字遵循字体及 Qt 的默认行为。

字体选择遵循原版字体的字形覆盖范围：英文、ASCII 数字及 Inter 已覆盖的符号优先使用 Inter，中文回退到 OPPO Sans 4.0。标点和其他符号同样按字形覆盖回退，不按全角／半角逐字符强制指定字体，也不生成派生字体。Emoji 保留平台回退。

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

加载器注册 OPPO 与全部 36 个 Inter 静态字体文件，保留应用原有字号，使用已知的 family 名配置字体候选列表 `Inter → OPPO Sans 4.0`，并注册 `OPPO Sans 4.0` 为 Han 脚本的应用回退字体。字号、字重和斜体仍可通过组件的 `font` 属性调整。常规界面继承统一候选列表；需要 Display 系列的组件可显式设置 `font.family: "Inter Display"`，Han 脚本仍使用应用配置的 OPPO 回退。

成功初始化后，重复调用不会重复注册字体。资源通过 `Q_INIT_RESOURCE(chunfe_font_assets)` 显式链接，路径以 `:/fonts/` 开头，不依赖工作目录。加载失败时打印具体资源路径、撤销本次已注册字体并返回 `false`。

## 许可证与声明

应用使用 **OPPO Sans 字体**。版权所有 © 2024 Guangdong OPPO Mobile Telecommunications Corp., Ltd. 原始许可位于 [`oppo-sans/OPPO Sans 4.0 License Notice.txt`](<oppo-sans/OPPO Sans 4.0 License Notice.txt>)；它要求在软件中显著声明使用 OPPO Sans，保留许可和版权声明，并随软件分发未修改的字体。Chunfe 主应用、TextBlock 实验室与启用项目字体的独立示例通过“字体许可”入口展示完整许可；初始化后的 `ChunfeFonts::licenseNotices()` 提供带标题的两份原文。该字体不能作为独立字体产品再分发。

Inter 与 Inter Display 的字体许可为 **SIL Open Font License 1.1**，版权所有 © 2016 The Inter Project Authors；原文位于 [`Inter-4.1/LICENSE.txt`](Inter-4.1/LICENSE.txt)。

两份原始许可证同时打包到 Qt 资源：

- `:/fonts/oppo-sans/OPPO Sans 4.0 License Notice.txt`
- `:/fonts/Inter-4.1/LICENSE.txt`

## SHA-256

用于校验随应用打包的原始文件。保留已记录的 OPPO 来源压缩包校验值；Inter 校验值直接计算自用户提供的来源目录，不推测压缩包校验值。压缩包本身不重复保存到仓库。

| 文件 | SHA-256 |
| --- | --- |
| `OPPO_Sans_4.0.zip` | `412d10fd4ed8447e31eec913a9bd4814816ce9614c12eaae23f77dcb0f54f97d` |
| `oppo-sans/OPPO Sans 4.0.ttf` | `6c7d5864c661516e1f400d9f21e4297f2e2a0719909691e29607cc4ef484a9f4` |
| `oppo-sans/OPPO Sans 4.0 License Notice.txt` | `b013f80fa2fa6f6db4a31b793676f787af78263b568088a2d896e9d9b3e5fde7` |
| `Inter-4.1/extras/ttf/Inter-Black.ttf` | `6342d3ea6dc088b43867f615e807d898adf100c93edb978b8e52c5eb71a264da` |
| `Inter-4.1/extras/ttf/Inter-BlackItalic.ttf` | `1737f7d5b391520e9adcc3ea8c730a0854fe6fdd9b9c93c4100c89beb215a931` |
| `Inter-4.1/extras/ttf/Inter-Bold.ttf` | `288316099b1e0a47a4716d159098005eef7c0066921f34e3200393dbdb01947f` |
| `Inter-4.1/extras/ttf/Inter-BoldItalic.ttf` | `948405a16cdc62701da5f4005ed068ca5f4d27061d98f7974ccfc37831d9581d` |
| `Inter-4.1/extras/ttf/Inter-ExtraBold.ttf` | `e6756ad5690b77606aa62249a7b420d9902d45cae4b0048a24911fd4324b0a22` |
| `Inter-4.1/extras/ttf/Inter-ExtraBoldItalic.ttf` | `31b58a00cb9e8d00cb936057285282310e81d2f834461264c543c96983af64a6` |
| `Inter-4.1/extras/ttf/Inter-ExtraLight.ttf` | `f3c7079d9eb4a799251a844acea9eb55f99094479306e2ac1bee875dd9f7ae80` |
| `Inter-4.1/extras/ttf/Inter-ExtraLightItalic.ttf` | `22fca887d9a9336e8a29601b19683947a57b53911c74e251c3657ee798635275` |
| `Inter-4.1/extras/ttf/Inter-Italic.ttf` | `bbc051dd204b5019a1aa0bc0ae2aa8a05ab13e7a3f979fa357631dc7feb6833a` |
| `Inter-4.1/extras/ttf/Inter-Light.ttf` | `164414f0aacbe98a7e64addc43f7b3bfd2e32f7b90e101feeab227f14c371bda` |
| `Inter-4.1/extras/ttf/Inter-LightItalic.ttf` | `c3f9efa776957eefaeac8a2991a990fd1bba6cb928dbaeab7abd0655f3a7693c` |
| `Inter-4.1/extras/ttf/Inter-Medium.ttf` | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `Inter-4.1/extras/ttf/Inter-MediumItalic.ttf` | `51c2c8d7c36f7c26e6e2678b5c3069b329bde9a081154553b0f5bc2d4fc14075` |
| `Inter-4.1/extras/ttf/Inter-Regular.ttf` | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `Inter-4.1/extras/ttf/Inter-SemiBold.ttf` | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `Inter-4.1/extras/ttf/Inter-SemiBoldItalic.ttf` | `eff2930c3d3b35d3fcf5f76252b6baef4c3e907d9d2fde1d16cf5d417f8deef4` |
| `Inter-4.1/extras/ttf/Inter-Thin.ttf` | `22453f995345e5618d539315a8501f4c74ca41d1898ce44e7d1b8206f0d097d4` |
| `Inter-4.1/extras/ttf/Inter-ThinItalic.ttf` | `d89ffb2849ef91246283de40a929f671b6e0c8ad1c235701f9362fb0406b4cce` |
| `Inter-4.1/extras/ttf/InterDisplay-Black.ttf` | `25460b0d5b3afd9764d63cf838f94145af624f8d4c9d20e0f74116be42878b32` |
| `Inter-4.1/extras/ttf/InterDisplay-BlackItalic.ttf` | `d4f3426d62f37f4960352201d109b07f4d50e689e321938770af1ca2d4729c67` |
| `Inter-4.1/extras/ttf/InterDisplay-Bold.ttf` | `b74c8e0dd744b3347faca4c96bc7b2e32f7d6f62300a79b1d1a99331e44a5bc4` |
| `Inter-4.1/extras/ttf/InterDisplay-BoldItalic.ttf` | `71e7d3709238b1c21b107ba6433d3a87c6377041a507afd9cf7de3c36ab585f2` |
| `Inter-4.1/extras/ttf/InterDisplay-ExtraBold.ttf` | `776d88313defcd42d33bc43bddcba6fc1e93ee3eaeca1c1ded2eacf2baec9d24` |
| `Inter-4.1/extras/ttf/InterDisplay-ExtraBoldItalic.ttf` | `857cf06880d9fe99f9d588a64386b875c2d7001c43624e086d0b8424b55bf415` |
| `Inter-4.1/extras/ttf/InterDisplay-ExtraLight.ttf` | `0d6606f81e4e5e98255e5194138229056380a74fcdebd779d4616d9ebc2819ed` |
| `Inter-4.1/extras/ttf/InterDisplay-ExtraLightItalic.ttf` | `9d055889c083f1f8869a2a9ea1440da9168e996f589ae43a36415a27c857a37b` |
| `Inter-4.1/extras/ttf/InterDisplay-Italic.ttf` | `20cf47556669f80d12966d2f5eac5054b7486ce5d076feb744dbe24842e2593b` |
| `Inter-4.1/extras/ttf/InterDisplay-Light.ttf` | `1d6072bc7807fa7bf08a6d273368f04ac68e77438258f32975057a61c8772811` |
| `Inter-4.1/extras/ttf/InterDisplay-LightItalic.ttf` | `07d4f953666f6e60022fc2fd6e62c48fe616bff7f2bf7eeaaeebc8febd01bfc9` |
| `Inter-4.1/extras/ttf/InterDisplay-Medium.ttf` | `99665c256585aeaab2d06950c2d7715cca8fd7883c2d0e7a421b8b287e1f17de` |
| `Inter-4.1/extras/ttf/InterDisplay-MediumItalic.ttf` | `443cfbc6d8013dd0c6f4f2a3952ee02a3b84a1ba44fd1456419b7e4f96f8c2a2` |
| `Inter-4.1/extras/ttf/InterDisplay-Regular.ttf` | `99614bda7ff423aaf470990692dd93613a5971ab4446e4a6d5a83b3d74865074` |
| `Inter-4.1/extras/ttf/InterDisplay-SemiBold.ttf` | `0310d7a325896129730c6c8cf9a6e0f81ee258bedf77b1ff059b2a7b75f74e02` |
| `Inter-4.1/extras/ttf/InterDisplay-SemiBoldItalic.ttf` | `7ff566a2fb8e811f6c846050444c3638b7d370fa3aabd08758fb8a0a2012b1ee` |
| `Inter-4.1/extras/ttf/InterDisplay-Thin.ttf` | `52762c933467ae114fd5b862c8cffdc289c8f93ef4073782fec1238d3d4a66df` |
| `Inter-4.1/extras/ttf/InterDisplay-ThinItalic.ttf` | `c3fc28a382edd767041e5ce3cf669cf8b09a150cffbe2086f798e8c5097fd257` |
| `Inter-4.1/LICENSE.txt` | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |
