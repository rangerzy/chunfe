# Chunfe 日志库

基于 Qt Core 的 C++ 日志库，使用 Qt 分类日志和已有的 Qt 输出处理器；不提供 QML 接口、文件输出或运行时插件机制。

## 接入

从源码使用时，引入本目录，并对每个写日志的 CMake target 调用一次 `cf_log_configure_target`：

```cmake
add_subdirectory(path/to/modules/logging)

add_library(audio ...)
cf_log_configure_target(audio) # 默认分类为 target 名称：audio

add_executable(my_app ...)
cf_log_configure_target(my_app app) # 可以显式指定模块分类
```

这个函数为调用目标添加日志依赖、模块分类和 `QT_MESSAGELOGCONTEXT`，都只作用于该目标，不会传播到其消费者。Release 构建也能在本目标的日志中记录文件和行号；使用其他 target 编译的第三方代码需要自行启用日志上下文，不能在此补录。

每个应用程序在 `main()` 最开始设置一次消息格式：

```cpp
#include "cf_log.h"

int main(int argc, char *argv[])
{
    CfLog::initialize();
    // 创建 QCoreApplication 或 QGuiApplication 并启动程序。
}
```

初始化使用 `qSetMessagePattern()` 设置格式，并安装进程级消息处理器，将源码路径缩短为文件名后转交初始化前的处理器（通常是 Qt 默认处理器）。初始化之前的消息不保证使用这个格式。Qt 的 `QT_MESSAGE_PATTERN` 环境变量优先，部分结构化日志后端也可能采用自身的呈现方式。如果之后安装其他消息处理器，需负责保留或替代这一处理。缩短路径时只能转发 Qt 公开的消息上下文；对于带路径的消息，Qt 内部保存的堆栈信息不会随之转发，因此 `QT_MESSAGE_PATTERN` 的 `%{backtrace}` 可能为空。

## 使用

```cpp
#include "cf_log.h"

DEBUG_CF_LOG() << "Capture started";
INFO_CF_LOG() << "Device:" << deviceName;
WARNING_CF_LOG() << "Device unavailable";
ERROR_CF_LOG().noquote() << "Failed to open:" << deviceName;
```

日志默认包含本地时间（毫秒）、等级、分类、源码文件和行号。输出沿用 Qt 的流式格式，包括空格和 `QString` 引号。`ERROR_CF_LOG` 对应 Qt 的 `critical` 级别，格式中显示为 `ERROR`，但过滤时仍使用 Qt 的级别名：

```sh
QT_LOGGING_RULES='audio.debug=false;audio.critical=false' ./my_app
```

被过滤的日志参数不会求值，避免在日志表达式中放入业务所依赖的副作用。请在 `.cpp` 中使用这些宏；跨多个 target 共用的公开 `inline` 或模板函数若直接使用按 target 配置的宏，会因不同 target 的分类不同而产生 C++ 定义冲突。应在对应模块的 `.cpp` 中记录日志。

## 测试

```sh
cmake -S modules/logging -B build/logging -DCHUNFE_LOG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/logging
ctest --test-dir build/logging --output-on-failure
```

主项目启用 `CHUNFE_BUILD_TESTS` 时默认也会启用日志测试。单独引入日志库时默认不要求 Qt Test。
