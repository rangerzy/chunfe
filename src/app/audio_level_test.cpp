#include "audio_level_test.h"

#include <QTimer>
#include <algorithm>
#include <utility>

using namespace AudioPipline;

AudioLevelTest::AudioLevelTest(AudioPiplineManager& manager, QObject* parent)
    : QObject(parent), m_manager(manager)
{
    connect(&manager, &AudioPiplineManager::requestFinished, this, &AudioLevelTest::handleRequest);
    connect(&manager, &AudioPiplineManager::captureChanged, this, &AudioLevelTest::handleCapture);
    connect(&manager, &AudioPiplineManager::issueReported, this, &AudioLevelTest::handleIssue);
    connect(&manager, &AudioPiplineManager::captureStarted, this,
            [this](const CaptureId& id, const CaptureInfo&) {
        if (m_active && id == m_captureId) m_hasRun = true;
    });
}

AudioLevelTest::~AudioLevelTest()
{
    // manager 的生命周期更长；只停止这个测试创建的会话。
    if (!m_stopping) stopInternal({}, true);
}

void AudioLevelTest::setRequestedStatus(const QString& text)
{
    if (!m_microphoneId.isEmpty() && !m_failed.testFlag(AudioSource::Mic)) m_microphoneStatus = text;
    if (!m_systemId.isEmpty() && !m_failed.testFlag(AudioSource::Spk)) m_systemStatus = text;
}

QString AudioLevelTest::userMessage(const AudioError& error, AudioSource source) const
{
    // 只有明确的权限错误才给授权指引，不能把设备或格式错误误报为权限问题。
    if (error.code == AudioErrorCode::PermissionDenied) {
        if (source == AudioSource::Mic)
            return tr("麦克风权限未授权，请在系统设置中允许使用麦克风");
        if (source == AudioSource::Spk)
            return tr("系统音频权限未授权，请在系统设置中允许录制系统音频");
        return tr("音频权限未授权，请检查系统设置");
    }
    return error.message;
}

void AudioLevelTest::fail(AudioSource source, const QString& reason)
{
    const auto message = reason.isEmpty() ? tr("采集失败") : reason;
    if (source == AudioSource::Mic) {
        m_failed |= source;
        m_microphoneLevel = 0;
        (m_stopping ? m_afterStopMicrophoneStatus : m_microphoneStatus) = message;
    } else if (source == AudioSource::Spk) {
        m_failed |= source;
        m_systemLevel = 0;
        (m_stopping ? m_afterStopSystemStatus : m_systemStatus) = message;
    }
}

void AudioLevelTest::start(const QString& microphoneId, const QString& systemId)
{
    if (m_active || m_stopping) return;
    ++m_generation;
    m_microphoneId = microphoneId;
    m_systemId = systemId;
    m_failed = {};
    m_attempted = {};
    m_hasRun = false;
    m_terminalFailure = false;
    m_stopRequested = false;
    m_microphoneLevel = m_systemLevel = 0;
    m_microphoneStatus = microphoneId.isEmpty() ? tr("无设备") : tr("正在启动");
    m_systemStatus = systemId.isEmpty() ? tr("无设备") : tr("正在启动");
    if (microphoneId.isEmpty() && systemId.isEmpty()) {
        emit changed();
        return;
    }
    if (m_manager.capture().state != CaptureState::Idle) {
        setRequestedStatus(tr("音频设备正被使用"));
        emit changed();
        return;
    }
    m_captureId.clear();
    m_active = true;
    if (!microphoneId.isEmpty()) {
        // 首次请求可能弹出系统权限框；结果异步返回，期间仍可取消测试。
        m_permissionRequest = m_manager.requestPermissions(AudioSource::Mic);
        emit changed();
        return;
    }
    beginCapture();
}

void AudioLevelTest::beginCapture()
{
    if (!m_active) return;
    // 权限回执到达时可能已有其他会话占用 manager，因此在启动前再次检查。
    if (m_manager.capture().state != CaptureState::Idle) {
        m_active = false;
        setRequestedStatus(tr("音频设备正被使用"));
        emit changed();
        return;
    }
    CaptureConfig config;
    if (!m_microphoneId.isEmpty() && !m_failed.testFlag(AudioSource::Mic)) {
        config.sources |= AudioSource::Mic;
        config.mic = {DeviceSelectionMode::Explicit, m_microphoneId};
    }
    if (!m_systemId.isEmpty() && !m_failed.testFlag(AudioSource::Spk)) {
        config.sources |= AudioSource::Spk;
        config.spk = {DeviceSelectionMode::Explicit, m_systemId};
    }
    if (!config.sources) {
        m_active = false;
        emit changed();
        return;
    }
    m_attempted = config.sources;
    m_hasRun = false;
    m_stopRequested = false;
    setRequestedStatus(tr("正在启动"));
    const auto result = m_manager.startCapture(config);
    if (!result.accepted) {
        m_active = false;
        auto messageFor = [&](AudioSource source) {
            if (result.error.code == AudioErrorCode::PermissionDenied && result.error.source != AudioSource::None &&
                result.error.source != source)
                return tr("采集启动失败");
            const auto message = userMessage(result.error, result.error.source);
            return message.isEmpty() ? tr("采集启动失败") : message;
        };
        if (m_attempted.testFlag(AudioSource::Mic)) fail(AudioSource::Mic, messageFor(AudioSource::Mic));
        if (m_attempted.testFlag(AudioSource::Spk)) fail(AudioSource::Spk, messageFor(AudioSource::Spk));
    } else {
        m_captureId = result.captureId;
    }
    emit changed();
}

void AudioLevelTest::handleRequest(const RequestId& id, const RequestResult& result)
{
    if (!m_active || id.isEmpty() || id != m_permissionRequest) return;
    m_permissionRequest.clear();
    if (result.status != RequestStatus::Completed) {
        auto message = userMessage(result.error, AudioSource::Mic);
        fail(AudioSource::Mic, message.isEmpty() ? tr("无法请求麦克风权限") : message);
    } else {
        for (const auto& permission : result.permissions) {
            if (permission.source == AudioSource::Mic &&
                (permission.state == PermissionState::Denied || permission.state == PermissionState::Restricted))
                fail(AudioSource::Mic, tr("麦克风权限未授权，请在系统设置中允许使用麦克风"));
        }
    }
    beginCapture();
}

void AudioLevelTest::stopInternal(const QString& reason, bool preserveErrors)
{
    // 先撤销意图和延迟重试，再取消权限请求或停止自有会话，防止回执重入复活测试。
    ++m_generation;
    m_active = false;
    if (!m_permissionRequest.isEmpty()) {
        const auto request = std::exchange(m_permissionRequest, {});
        m_manager.cancelRequest(request);
    }
    m_microphoneLevel = m_systemLevel = 0;
    if (m_stopping) {
        m_microphoneStatus = m_afterStopMicrophoneStatus;
        m_systemStatus = m_afterStopSystemStatus;
    }
    const auto message = reason.isEmpty() ? tr("已停止") : reason;
    if (!preserveErrors) m_failed = {};
    setRequestedStatus(message);
    m_afterStopMicrophoneStatus = m_microphoneStatus;
    m_afterStopSystemStatus = m_systemStatus;
    if (!m_captureId.isEmpty() && !m_stopRequested) {
        const auto snapshot = m_manager.capture();
        if (snapshot.captureId == m_captureId && snapshot.state != CaptureState::Idle) {
            m_stopRequested = true;
            m_stopping = true;
            if (!m_microphoneId.isEmpty()) m_microphoneStatus = tr("正在停止");
            if (!m_systemId.isEmpty()) m_systemStatus = tr("正在停止");
            const auto stopped = m_manager.stopCapture(m_captureId, StopMode::Discard);
            if (!stopped.accepted) {
                m_stopping = false;
                m_stopRequested = false;
                m_captureId.clear();
                const auto failure = stopped.error.message.isEmpty() ? tr("停止采集失败") : stopped.error.message;
                if (!m_microphoneId.isEmpty()) m_microphoneStatus = failure;
                if (!m_systemId.isEmpty()) m_systemStatus = failure;
            }
        } else if (snapshot.state == CaptureState::Idle) {
            m_captureId.clear();
        }
    }
    emit changed();
}

void AudioLevelTest::stop()
{
    if (m_stopping) {
        if (m_active) {
            ++m_generation;
            m_active = false;
            if (!m_microphoneId.isEmpty() && !m_failed.testFlag(AudioSource::Mic)) m_afterStopMicrophoneStatus = tr("已停止");
            if (!m_systemId.isEmpty() && !m_failed.testFlag(AudioSource::Spk)) m_afterStopSystemStatus = tr("已停止");
            emit changed();
        }
        return;
    }
    if (!m_active && m_captureId.isEmpty() && m_permissionRequest.isEmpty()) return;
    stopInternal({}, true);
}

void AudioLevelTest::setDevices(const QString& microphoneId, const QString& systemId)
{
    if (microphoneId == m_microphoneId && systemId == m_systemId) return;
    ++m_generation;
    const bool wasActive = m_active;
    const bool microphoneChanged = microphoneId != m_microphoneId;
    const bool systemChanged = systemId != m_systemId;
    if (wasActive) stopInternal(tr("设备已变更，请重新测试"), true);
    m_microphoneId = microphoneId;
    m_systemId = systemId;
    auto status = [&](bool changed, const QString& id, QString& displayed, QString& afterStop) {
        if (!changed) return;
        const auto text = id.isEmpty() ? tr("无设备") :
            (wasActive ? tr("设备已变更，请重新测试") : tr("等待测试"));
        if (m_stopping) afterStop = text;
        else displayed = text;
    };
    status(microphoneChanged, microphoneId, m_microphoneStatus, m_afterStopMicrophoneStatus);
    status(systemChanged, systemId, m_systemStatus, m_afterStopSystemStatus);
    emit changed();
}

void AudioLevelTest::handleIssue(const AudioIssue& issue)
{
    if (m_captureId.isEmpty() || issue.captureId != m_captureId) return;
    if (issue.error.code == AudioErrorCode::BackendStopFailed) {
        if (m_stopping) {
            if (!m_microphoneId.isEmpty()) m_microphoneStatus = tr("停止失败，正在重试");
            if (!m_systemId.isEmpty()) m_systemStatus = tr("停止失败，正在重试");
            emit changed();
        }
        return;
    }
    if (!m_active || issue.error.code == AudioErrorCode::QueueFull) return;
    if (issue.error.code == AudioErrorCode::DeviceDisconnected &&
        issue.error.source != AudioSource::Mic && issue.error.source != AudioSource::Spk) {
        auto message = userMessage(issue.error, AudioSource::None);
        if (message.isEmpty()) message = tr("设备已断开");
        if (m_attempted.testFlag(AudioSource::Mic)) fail(AudioSource::Mic, message);
        if (m_attempted.testFlag(AudioSource::Spk)) fail(AudioSource::Spk, message);
        m_terminalFailure = true;
        stopInternal(tr("设备已断开，请重新测试"), true);
        return;
    }
    if (issue.error.source == AudioSource::Mic || issue.error.source == AudioSource::Spk) {
        // 设备断开时整个测试停止；队列过载等提示不等同于采集停止。
        if (!m_hasRun || issue.error.code == AudioErrorCode::DeviceDisconnected) {
            fail(issue.error.source, userMessage(issue.error, issue.error.source));
            if (issue.error.code == AudioErrorCode::DeviceDisconnected) {
                stopInternal(tr("设备已断开，请重新测试"), true);
                return;
            }
        }
    } else if (!m_hasRun) {
        m_terminalFailure = true;
        auto message = userMessage(issue.error, AudioSource::None);
        if (message.isEmpty()) message = tr("采集启动失败");
        if (m_attempted.testFlag(AudioSource::Mic)) fail(AudioSource::Mic, message);
        if (m_attempted.testFlag(AudioSource::Spk)) fail(AudioSource::Spk, message);
    }
    emit changed();
}

void AudioLevelTest::handleCapture(const CaptureSnapshot& snapshot)
{
    if (m_captureId.isEmpty() || snapshot.captureId != m_captureId) return;
    bool disconnected = false;
    for (const auto& channel : snapshot.channels) {
        if (channel.source != AudioSource::Mic && channel.source != AudioSource::Spk) continue;
        if (channel.state == ChannelState::Failed && m_active) {
            fail(channel.source, userMessage(channel.error, channel.source));
            disconnected |= m_hasRun || channel.error.code == AudioErrorCode::DeviceDisconnected;
        } else if (m_active && snapshot.state == CaptureState::Running && channel.state == ChannelState::Running) {
            const auto level = std::clamp<qreal>(channel.peak, 0, 1);
            if (channel.source == AudioSource::Mic) { m_microphoneLevel = level; m_microphoneStatus = tr("正在测试"); }
            else { m_systemLevel = level; m_systemStatus = tr("正在测试"); }
        }
    }
    if (disconnected) {
        stopInternal(tr("设备已断开，请重新测试"), true);
        return;
    }
    if (snapshot.state == CaptureState::Stopping && m_active && !m_stopping) {
        m_stopping = true;
        m_afterStopMicrophoneStatus = m_microphoneStatus;
        m_afterStopSystemStatus = m_systemStatus;
        if (!m_microphoneId.isEmpty()) m_microphoneStatus = tr("正在停止");
        if (!m_systemId.isEmpty()) m_systemStatus = tr("正在停止");
    }
    if (snapshot.state == CaptureState::Idle) {
        m_captureId.clear();
        m_stopRequested = false;
        if (m_stopping) {
            m_stopping = false;
            m_microphoneStatus = m_afterStopMicrophoneStatus;
            m_systemStatus = m_afterStopSystemStatus;
            m_microphoneLevel = m_systemLevel = 0;
            if (!m_active) {
                emit changed();
                return;
            }
        }
        if (m_active && !m_hasRun && !m_terminalFailure && m_failed &&
            m_attempted == (AudioSource::Mic | AudioSource::Spk) &&
            m_failed != (AudioSource::Mic | AudioSource::Spk)) {
            // 首次双路启动是原子的：一条失败后，等旧会话 Idle 再尝试另一条。
            // 延后一轮可避免在 Idle 信号分发中改写快照；操作编号防止旧重试复活。
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] {
                if (m_active && !m_stopping && m_captureId.isEmpty() && generation == m_generation)
                    beginCapture();
            });
            emit changed();
            return;
        }
        if (m_active) {
            m_active = false;
            if (!m_failed) setRequestedStatus(tr("采集已停止"));
        }
        m_microphoneLevel = m_systemLevel = 0;
    }
    emit changed();
}
