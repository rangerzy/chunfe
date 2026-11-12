#ifndef CHUNFE_MAC_SYSTEM_CAPTURE_H
#define CHUNFE_MAC_SYSTEM_CAPTURE_H

#include <QString>
#include <cstdint>
#include <functional>
#include <memory>

// macOS 系统输出采集：控制方法须在同一个非音频线程调用。
class MacSystemCapture
{
public:
    // Core Audio 线程同步交付 16 kHz、单声道、有符号 16 位 PCM；最后一个参数是该输出块
    // 末尾到原始输入块末尾的近似纳秒数，供接收方分块估算时间戳（滤波延迟未精确补偿）。
    // 数据指针仅在本次回调期间有效，接收方不能阻塞或在返回后继续引用。
    using Callback = void (*)(void*, const std::int16_t*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint64_t);
    // 故障通知运行在 Core Audio 通知线程；停止和资源清理应派发回控制线程。
    using FailureCallback = void (*)(void*, const char*);
    enum class Permission { NotDetermined, Granted, Denied, Restricted };
    enum class DeviceDirection { Input, Output };

    MacSystemCapture();
    ~MacSystemCapture();
    MacSystemCapture(const MacSystemCapture&) = delete;
    MacSystemCapture& operator=(const MacSystemCapture&) = delete;

    // 空 deviceUid 在准备时解析并固定当时的默认输出；prepare 创建资源但不启动采集。
    bool prepare(const QString& deviceUid, Callback callback, void* userData, QString* error,
                 FailureCallback failure = nullptr);
    // 重新确认设备和格式没有变化后启动；系统音频授权由 macOS 处理。
    bool start(QString* error);
    // false 表示仍有 Core Audio 资源未释放；再次调用可继续尝试清理。
    bool stop(QString* error = nullptr);

    QString deviceUid() const;
    QString deviceName() const;
    std::uint32_t sampleRate() const;
    std::uint32_t channels() const;

    static bool isSupported() noexcept;
    // 只读检查设备存在、存活及输入/输出流；不打开设备，也不能保证权限或后续启动成功。
    static bool isCaptureCandidate(const QString& deviceUid, DeviceDirection direction);
    static Permission microphonePermission();
    // 麦克风授权与系统输出采集授权不同；结果异步返回，回调线程由系统决定。
    static void requestMicrophonePermission(std::function<void(bool)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

#endif
