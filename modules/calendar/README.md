# Calendar

从 [`examples/calendar_before_today`](../../examples/calendar_before_today) 提取的静态 QML 日历模块。公开组件为 `PastDateCalendar`，保留中文年月选择、周日首列、六周网格、日期数据圆点和液态选中动画。可选择范围为起始月份第一天到今天，包含两端。

- QML URI：`Calendar`；公开组件：`PastDateCalendar`
- CMake targets：`calendar`、`calendarplugin`
- 环境：Qt 6.12+（Quick、QuickControls2 及其基础库）、CMake 3.24+
- 模块不依赖 Chunfe 主应用、字体资源或业务数据；`LiquidSelection` 是内部实现。

## 接入

在已调用 `qt_standard_project_setup(REQUIRES 6.12)` 的工程中：

```cmake
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
add_subdirectory("/absolute/path/to/modules/calendar" "${CMAKE_CURRENT_BINARY_DIR}/calendar")
target_link_libraries(my_app PRIVATE calendar calendarplugin)
```

在应用入口显式注册静态 QML 插件：

```cpp
#include <QQmlExtensionPlugin>
Q_IMPORT_QML_PLUGIN(CalendarPlugin)
```

QML 中直接创建控件：

```qml
import QtQuick
import Calendar

PastDateCalendar {
    minimumYear: 2025
    minimumMonth: 1
    onDateSelected: date => console.log(Qt.formatDate(date, "yyyy-MM-dd"))
    onMonthChanged: (year, month) => {
        // 用该月份的业务数据替换列表；这里仅演示每月第一天有数据。
        dataDates = [Qt.formatDate(new Date(year, month - 1, 1), "yyyy-MM-dd")]
    }
}
```

## 日期与事件

| 属性 | 含义 |
| --- | --- |
| `minimumYear`、`minimumMonth` | 最早可选年月；月份为 1–12，默认 `today` 所在年的 1 月 |
| `today` | 最晚可选日期；默认系统本地日期，30 秒检查一次跨日变化；显式传入值或绑定时由调用方控制 |
| `selectedDate` | 当前选择，默认 `today`；直接赋值只更新选择，不发送选择信号 |
| `displayedYear`、`displayedMonth` | 展示年月，默认 `today` 所在月；月份为 1–12 |
| `dataDates` | 有数据的本地日期字符串列表，格式为 `yyyy-MM-dd`；默认空列表 |
| `dataDotColor` | 数据圆点颜色，默认 `#FA8C16` |
| `animationDuration` | 日期间移动动画时长，默认 240 毫秒 |
| `liquidStretch` | 液滴拉伸强度，默认 1 |

调用方应传入有效日期和不晚于 `today` 的起始年月。`selectDate(date)` 检查日期范围，更新选择并发送 `dateSelected(date)`；重复选择同一天也发送信号。日期比较按本地年月日进行，忽略时分秒。`selectedDate`、`today` 均应使用本地 `new Date(year, month - 1, day)` 构造，避免 UTC 字符串解析导致日期偏移。

`showMonth(year * 12 + month - 1)` 在合法范围内切换月份，不改变已选日期。直接赋值 `displayedYear` / `displayedMonth` 不校验范围，日常导航建议使用 `showMonth()`。切换年份和月份的连续赋值会合并通知。

`monthChanged(year, month)` 在初始化完成后通知一次，之后仅在展示月份实际变化时发送，月份为 1–12。调用方可在这里加载数据并整体替换 `dataDates`。异步加载时应确认返回结果仍对应当前展示年月；控件不负责请求或缓存业务数据。

选择网格中的相邻月份日期时，`selectedDate` 和 `dateSelected` 立即更新，液滴到达目标后再切换展示月。快速点击会重定向动画；手动切月和调整尺寸会取消旧动画。导航不会清除其他月份的选择。

## 布局与外观

默认隐式尺寸为 492×532。可通过 `anchors.fill`、`width` / `height` 或布局调整大小，内容按较小尺寸缩放，最大缩放比例为 1。字体继承应用的 Qt Quick Controls 字体配置；可设置继承自 `Pane` 的 `font`、`background` 等属性。默认界面为中文、周日首列。

模块不加载字体或设置全局样式。独立示例在应用层选择性加载仓库字体。内部动画、几何辅助方法和以下划线开头的状态不是稳定接入 API。

## 构建与测试

[`examples/calendar_consumer`](../../examples/calendar_consumer) 是新的独立接入工程，实际导入并链接本模块。原 `calendar_before_today` 示例保留为原型参考。

```sh
cmake -S examples/calendar_consumer -B build/calendar-consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/platform" \
  -DCALENDAR_BUILD_TESTS=ON
cmake --build build/calendar-consumer
cmake --build build/calendar-consumer --target calendar_qmllint calendar_consumer_qmllint
ctest --test-dir build/calendar-consumer --output-on-failure
build/calendar-consumer/calendar_consumer
```

根工程也引入模块；首次配置时 `CALENDAR_BUILD_TESTS` 默认跟随 `CHUNFE_BUILD_TESTS`，可单独覆盖。其他消费者开启模块测试时应先在顶层调用 `enable_testing()`。

`calendar_tests` 显式导入静态插件，通过公开 URI 创建控件，使用固定日期与独立数据验证范围、跨年、闰日、年月选择、信号、动画、数据标记和多实例隔离。测试采用 offscreen / software 后端，实际渲染仍应在桌面检查。
