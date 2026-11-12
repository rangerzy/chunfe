# 春风品牌图形

2026-11-03 用户批准采用“01 错位双引号”：开、闭引号相反朝向，组成一问一答。

- `chunfe-mark.svg`：深色符号，透明背景，用于浅色表面和白色悬浮球。
- `chunfe-mark-light.svg`：浅色符号，透明背景，用于深色侧栏图标底。
- `chunfe-app.svg`：深色圆角方底与浅色符号，用于网页及应用图标。

原型 `prototypes/interview-assistant/assets/` 中保留相同文件的静态服务副本。Linux 图标使用既有路径 `packaging/linux/io.github.rangerzy.chunfe.svg`，尺寸为 128×128，几何形状相同。修改图形时请同步这些副本。图形源文件均为 SVG，支持缩放且不依赖外部字体。

## 更新桌面图标

`chunfe-app.icns` 和 `chunfe-app.ico` 由 `chunfe-app.svg` 导出，构建直接使用这两个文件。修改应用图标时，用 Qt SVG 等渲染器按目标尺寸分别渲染透明背景的 PNG，再更新这两个文件。

- macOS 的 `.iconset` 包含 16、32、128、256、512 像素的常规图标及各自的 2 倍尺寸。按 `icon_16x16.png` 和 `icon_16x16@2x.png` 的规则命名，用 `iconutil -c icns Chunfe.iconset -o chunfe-app.icns` 封装。可用 `iconutil -c iconset chunfe-app.icns` 检查所有尺寸。
- Windows 的应用与安装器共用一个 ICO。将 PNG 按像素尺寸命名后，用 `magick 16.png 32.png 48.png 64.png 128.png 256.png chunfe-app.ico` 封装。
