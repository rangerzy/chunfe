#ifndef CHUNFE_AUDIO_DEVICE_LIST_H
#define CHUNFE_AUDIO_DEVICE_LIST_H

#include <QObject>
#include <QVariantList>
#include <audio_pipline_manager.h>
#include <functional>

class QMediaDevices;
class QTimer;

// 把采集层的设备快照转换为 QML 列表。Qt Multimedia 只负责通知设备变化，
// 设备身份、可用性和默认标记始终来自 AudioPiplineManager。
class AudioDeviceList final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList microphoneDevices READ microphoneDevices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList systemDevices READ systemDevices NOTIFY devicesChanged)

public:
    // 可注入刷新入口，供测试控制异步请求的完成时机。
    using RefreshRequest = std::function<AudioPipline::RequestId()>;
    explicit AudioDeviceList(AudioPiplineManager& manager, QObject* parent = nullptr);
    AudioDeviceList(AudioPiplineManager& manager, RefreshRequest request, QObject* parent = nullptr);

    // 列表项包含 id、name、isDefault 和带默认标记的 displayName。
    QVariantList microphoneDevices() const { return m_microphoneDevices; }
    QVariantList systemDevices() const { return m_systemDevices; }
    // 异步请求刷新；短时间内的多次请求会合并，不等待设备枚举完成。
    Q_INVOKABLE void refresh();

signals:
    void devicesChanged();

private:
    void updateDevices(const AudioPipline::EnvironmentSnapshot& environment);
    void startRefresh();
    void refreshFinished(const AudioPipline::RequestId& id);

    AudioPiplineManager& m_manager;
    RefreshRequest m_refreshRequest;
    QMediaDevices* m_mediaDevices;
    QTimer* m_refreshTimer;
    AudioPipline::RequestId m_refreshInFlight; // 正在等待结果的刷新请求。
    bool m_refreshPending = false;             // 在途期间又有变化，完成后需再刷新。
    QVariantList m_microphoneDevices;
    QVariantList m_systemDevices;
};

#endif // CHUNFE_AUDIO_DEVICE_LIST_H
