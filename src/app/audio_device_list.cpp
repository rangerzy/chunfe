#include "audio_device_list.h"

#include <QMediaDevices>
#include <QTimer>
#include <algorithm>
#include <utility>

AudioDeviceList::AudioDeviceList(AudioPiplineManager& manager, QObject* parent)
    : AudioDeviceList(manager, [&manager] { return manager.refreshDevices(); }, parent)
{
}

AudioDeviceList::AudioDeviceList(AudioPiplineManager& manager, RefreshRequest request, QObject* parent)
    : QObject(parent), m_manager(manager), m_refreshRequest(std::move(request)),
      m_mediaDevices(new QMediaDevices(this)), m_refreshTimer(new QTimer(this))
{
    connect(&m_manager, &AudioPiplineManager::environmentChanged,
            this, &AudioDeviceList::updateDevices);
    connect(&m_manager, &AudioPiplineManager::requestFinished, this,
            [this](const auto& id, const auto&) { refreshFinished(id); });
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(50);
    connect(m_refreshTimer, &QTimer::timeout, this, &AudioDeviceList::startRefresh);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &AudioDeviceList::refresh);
    connect(m_mediaDevices, &QMediaDevices::audioOutputsChanged, this, &AudioDeviceList::refresh);
    updateDevices(m_manager.environment());
    // 监听安装后再刷新一次，避免错过初始快照与注册通知之间的设备变化。
    refresh();
}

void AudioDeviceList::refresh()
{
    // 已有请求在途时只记下待刷新标记，避免慢枚举造成请求堆积。
    m_refreshPending = true;
    if (m_refreshInFlight.isEmpty() && !m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void AudioDeviceList::startRefresh()
{
    if (!m_refreshPending || !m_refreshInFlight.isEmpty()) return;
    m_refreshPending = false;
    m_refreshInFlight = m_refreshRequest();
}

void AudioDeviceList::refreshFinished(const AudioPipline::RequestId& id)
{
    if (id != m_refreshInFlight || id.isEmpty()) return;
    m_refreshInFlight.clear();
    // 在途期间发生的变化必须再查一次，不能由刚完成的旧快照代替。
    if (m_refreshPending && !m_refreshTimer->isActive()) m_refreshTimer->start();
}

void AudioDeviceList::updateDevices(const AudioPipline::EnvironmentSnapshot& environment)
{
    // 失效快照、不可用设备和不支持的来源都不能当作当前可选设备。
    QVariantList microphones;
    QVariantList systemOutputs;
    for (const auto& device : environment.devices) {
        if (environment.stale || !device.available ||
            !std::any_of(environment.capabilities.cbegin(), environment.capabilities.cend(),
                         [&device](const auto& capability) {
                             return capability.source == device.source && capability.supported;
                         }))
            continue;
        const QVariantMap item{{QStringLiteral("id"), device.id},
                               {QStringLiteral("name"), device.name},
                               {QStringLiteral("isDefault"), device.isDefault},
                               {QStringLiteral("displayName"), device.isDefault
                                    ? device.name + tr("（默认）") : device.name}};
        if (device.source == AudioPipline::AudioSource::Mic)
            microphones.append(item);
        else if (device.source == AudioPipline::AudioSource::Spk)
            systemOutputs.append(item);
    }
    if (m_microphoneDevices == microphones && m_systemDevices == systemOutputs)
        return;
    m_microphoneDevices = std::move(microphones);
    m_systemDevices = std::move(systemOutputs);
    emit devicesChanged();
}
