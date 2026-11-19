# Chunfe.Ui

Qt 6.12 静态 QML 展示模块，仅依赖 Qt Quick / Controls Basic / Layouts。不读取文件、请求网络或管理应用状态，不绑定品牌字体。

通过 `add_subdirectory(modules/ui_controls)` 引入，链接 `ui_controls` 和 `ui_controlsplugin`，QML 使用 `import Chunfe.Ui`。

| 类型 | 公开接口 |
| --- | --- |
| `Theme` | 只读单例颜色：`background`、`surface`、`text`、`muted`、`border`、`primary`、`primaryText`、`hover`、`danger`、`ring`、`darkBackground`、`darkSurface`、`darkText`、`darkMuted`、`darkBorder`；`radius` 为 10 |
| `AppButton` | 标准 Button 属性/信号，另有 `variant`（primary / secondary / ghost / danger）、`iconName`、`cornerRadius`（默认跟随 Theme）；默认高 36 |
| `IconButton` | 标准 Button 属性/信号；`iconName`、`dark`，`text` 用于无障碍名称和悬停提示；默认 32 × 32 |
| `AppField` | 标准 TextField 属性/信号，默认高 38，支持鼠标选择和密码模式 |
| `StatusBadge` | `text`、`tone`（neutral / success / warning / danger）、`dark`；success 沿用 Neutral 配色 |
| `PageHeader` | `title`、`subtitle`；调用方分配宽度，高度由内容决定 |
| `SettingRow` | `label`、`description`、`labelWidth`（136）、`spacing`（20）、`minimumHeight`（42）；默认子项进入右侧 ColumnLayout，使用 `Layout.fillWidth` 填满控件列，说明文字与控件左边缘对齐 |
| `AppIcon` | `name`、`color`、`size`（默认 20），支持布局覆盖宽高 |

图标采用官方 **IconPark / @icon-park/svg 1.4.2** 的线性 SVG，按 48 × 48 网格、4 单位圆角线宽导出，未修改任何几何路径。原始导出位于 `icons/*.svg`，`IconParkData.js` 是生成后的离线索引。`AppIcon` 只替换前景色并通过 Qt 标准 `Image` / SVG 图像插件显示，按设备像素比例栅格化；支持任意 `color` 及透明度，不依赖图标字体、私有 Qt 控件、着色器或网络。图标名：home、history、user、model、audio、arrow-right、plus、check、close、chevron-left、chevron-right、search、file、upload、download、trash、edit、play、pause、stop、collapse、expand、grip、clock、headphones、mic、settings、alert、eye、bolt、info。未知名称为空。

控件继承应用字体。按钮支持 Tab 聚焦、键盘触发、禁用态和可见焦点环。窗口拖动、页面导航与提交行为由调用方处理。

## 图标来源与更新

- 官方网站：https://iconpark.oceanengine.com/official
- 官方仓库：https://github.com/bytedance/IconPark
- 固定包：`@icon-park/svg@1.4.2`；来源提交、归档 SHA-256、各图标官方名称及源文件校验值见 `icons/SOURCE.json`。
- Copyright 2019-present Bytedance Inc.，Apache-2.0；完整许可及转换说明位于 `icons/LICENSE`、`icons/NOTICE`，并包含在模块资源中。

需要更新资源时，从 npm 官方 registry 下载固定包并解压，然后执行：

```sh
node modules/ui_controls/icons/generate.cjs /absolute/path/to/extracted/package
```

该脚本调用官方 SVG renderer 的 outline 输出，重新生成可审阅的 SVG、运行时索引和来源清单。不会手工重绘图标。新名称需要同步脚本中的别名映射。
