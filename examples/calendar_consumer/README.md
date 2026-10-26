# Calendar 独立接入示例

这个独立的 Qt 6.12 / CMake 3.24 项目通过 `import Calendar` 使用 `PastDateCalendar`，不依赖 Chunfe 应用代码。仓库根项目不会构建此示例。

在仓库根目录运行：

```sh
cmake -S examples/calendar_consumer -B build/calendar-consumer -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos
cmake --build build/calendar-consumer
./build/calendar-consumer/calendar_consumer
```

将 `CMAKE_PREFIX_PATH` 替换为当前平台的 Qt 安装前缀。此示例生成普通可执行文件，在 macOS 上不创建 `.app` bundle；Windows 下文件名为 `calendar_consumer.exe`。构建目录应与根应用及其他示例分开。

日历保留原示例的视觉与交互。最早可选月份为当前月份往前 3 个月，截止日期为今天；底部会随选择更新日期。每次 `monthChanged(year, month)` 时，示例为当月 1、6、14、22 日中不晚于今天的日期填入 `dataDates`，以橙色圆点表示记录。实际接入时可以将该处理替换成自己的数据查询。

`CALENDAR_SOURCE_DIR` 默认指向仓库内的 `modules/calendar`。将示例复制到其他位置时，可以显式指定模块目录，并使用系统字体：

```sh
cmake -S /path/to/calendar_consumer -B /path/to/consumer-build -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos \
  -DCALENDAR_SOURCE_DIR=/path/to/modules/calendar \
  -DCHUNFE_FONTS_SOURCE_DIR=
cmake --build /path/to/consumer-build
/path/to/consumer-build/calendar_consumer
```

接入自己的应用时，在 CMake 中通过 `add_subdirectory` 引入模块并链接 `calendar`、`calendarplugin`；在 C++ 入口添加 `Q_IMPORT_QML_PLUGIN(CalendarPlugin)`；在 QML 中 `import Calendar` 后即可使用 `PastDateCalendar`。年月参数中的月份使用 1–12，`dataDates` 使用本地日期的 `yyyy-MM-dd` 字符串。响应 `dateSelected(date)` 可处理用户选择，也可以绑定 `selectedDate` 展示当前选择。

仓库字体资源存在时会默认启用，窗口底部提供字体许可入口。`CHUNFE_FONTS_SOURCE_DIR` 可指定其他兼容的字体资源目录；传入空值时使用系统字体，此时只需 Qt 和 Calendar 模块源码。QML 编辑器配置优先使用仓库的 `ChunfeQmlTooling.cmake`，复制到其他目录后会自动使用独立配置。

启用模块测试：

```sh
cmake -S examples/calendar_consumer -B build/calendar-consumer -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos \
  -DCALENDAR_BUILD_TESTS=ON
cmake --build build/calendar-consumer
ctest --test-dir build/calendar-consumer --output-on-failure
```

保存窗口截图后退出：

```sh
./build/calendar-consumer/calendar_consumer --screenshot /tmp/calendar-consumer.png
```
