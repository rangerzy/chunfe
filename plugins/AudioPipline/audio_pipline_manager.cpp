#include "audio_pipline_manager.h"
#include "captures/audio_capture_context.h"

#include <QMetaObject>
#include <QTimer>
#include <QUuid>

using namespace AudioPipline;
namespace {
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
AudioError error(AudioErrorCode code, const QString& message)
{
    return {code, message};
}
bool contains(AudioSources sources, AudioSource source) { return sources.testFlag(source); }
}

AudioPiplineManager::AudioPiplineManager(QObject* parent)
    : QObject(parent), m_captureContext(std::make_shared<AudioCaptureContext>()), m_levelTimer(new QTimer(this))
{
    m_environment = m_captureContext->snapshot();
    m_levelTimer->setInterval(50);
    connect(m_levelTimer, &QTimer::timeout, this, [this] {
        if (m_captureSnapshot.state != CaptureState::Running || !m_levelsDirty) return;
        m_levelsDirty = false;
        emit captureChanged(m_captureSnapshot);
    });
}

AudioPiplineManager::~AudioPiplineManager()
{
    m_shuttingDown = true;
    m_levelTimer->stop();
    if (m_captureThread.isRunning()) {
        // 先在 worker 所在线程关闭回调入口并尝试停机，再退出线程；停机失败时由 worker 保留必要的回调上下文。
        QMetaObject::invokeMethod(m_micWorker, &AudioCaptureWorker::shutdown, Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(m_spkWorker, &AudioCaptureWorker::shutdown, Qt::BlockingQueuedConnection);
        m_captureThread.quit();
        m_captureThread.wait();
    }
}

bool AudioPiplineManager::isCurrent(const CaptureId& id, quint64 operation) const
{
    // 相同 captureId 可以暂停后恢复；仅检查 ID 会把旧操作的回执误认为当前回执。
    return !m_shuttingDown && m_captureSnapshot.captureId == id && m_operation == operation;
}

void AudioPiplineManager::ensureCaptureWorkers()
{
    if (m_micWorker) return;
    m_micWorker = new AudioCaptureWorker(AudioSource::Mic, m_captureContext);
    m_spkWorker = new AudioCaptureWorker(AudioSource::Spk, m_captureContext);
    for (auto* worker : {m_micWorker, m_spkWorker}) {
        worker->moveToThread(&m_captureThread);
        connect(&m_captureThread, &QThread::finished, worker, &QObject::deleteLater);
        connect(this, &AudioPiplineManager::prepareSourceRequested, worker, &AudioCaptureWorker::prepareSource, Qt::QueuedConnection);
        connect(this, &AudioPiplineManager::startSourceRequested, worker, &AudioCaptureWorker::startSource, Qt::QueuedConnection);
        connect(this, &AudioPiplineManager::confirmSourceRequested, worker, &AudioCaptureWorker::confirmSource, Qt::QueuedConnection);
        connect(this, &AudioPiplineManager::activateSourceRequested, worker, &AudioCaptureWorker::activateSource, Qt::QueuedConnection);
        connect(this, &AudioPiplineManager::stopSourceRequested, worker, &AudioCaptureWorker::stopSource, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::prepared, this, &AudioPiplineManager::onPrepared, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::started, this, &AudioPiplineManager::onStarted, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::confirmed, this, &AudioPiplineManager::onConfirmed, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::stopped, this, &AudioPiplineManager::onStopped, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::sourceFailed, this, &AudioPiplineManager::onSourceFailed, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::issueReported, this, [this](const AudioIssue& issue) {
            if (issue.captureId == m_captureSnapshot.captureId) emit issueReported(issue);
        }, Qt::QueuedConnection);
        connect(worker, &AudioCaptureWorker::levelChanged, this,
                [this](const CaptureId& id, AudioSource source, float peak, float rms) {
            if (id != m_captureSnapshot.captureId || m_captureSnapshot.state != CaptureState::Running) return;
            for (auto& channel : m_captureSnapshot.channels) {
                if (channel.source != source || channel.state != ChannelState::Running) continue;
                channel.peak = peak;
                channel.rms = rms;
                channel.hasSignal = peak > 0.001f;
                m_levelsDirty = true;
            }
        }, Qt::QueuedConnection);
    }
    connect(this, &AudioPiplineManager::refreshEnvironmentRequested, m_micWorker, &AudioCaptureWorker::refreshEnvironment, Qt::QueuedConnection);
    connect(this, &AudioPiplineManager::checkAvailabilityRequested, m_micWorker, &AudioCaptureWorker::checkAvailability, Qt::QueuedConnection);
    connect(this, &AudioPiplineManager::permissionsRequested, m_micWorker, &AudioCaptureWorker::requestPermissions, Qt::QueuedConnection);
    connect(m_micWorker, &AudioCaptureWorker::environmentRequestFinished, this, &AudioPiplineManager::onEnvironmentResult, Qt::QueuedConnection);
    m_captureThread.start();
}

void AudioPiplineManager::publishEnvironment(EnvironmentSnapshot environment)
{
    environment.revision = m_environment.revision + 1;
    m_environment = std::move(environment);
    emit environmentChanged(m_environment);
}

void AudioPiplineManager::onEnvironmentResult(RequestId id, RequestResult result)
{
    if (!m_environmentRequests.remove(id)) return;
    publishEnvironment(result.environment);
    result.environment = m_environment;
    emit requestFinished(id, result);
}

RequestId AudioPiplineManager::refreshDevices()
{
    ensureCaptureWorkers();
    const auto id = token();
    m_environmentRequests.insert(id, RequestKind::RefreshDevices);
    emit refreshEnvironmentRequested(id);
    return id;
}
RequestId AudioPiplineManager::checkAvailability(const CaptureConfig& config)
{
    ensureCaptureWorkers();
    const auto id = token();
    m_environmentRequests.insert(id, RequestKind::CheckAvailability);
    emit checkAvailabilityRequested(id, config);
    return id;
}
RequestId AudioPiplineManager::requestPermissions(AudioSources sources)
{
    ensureCaptureWorkers();
    const auto id = token();
    m_environmentRequests.insert(id, RequestKind::RequestPermissions);
    emit permissionsRequested(id, sources);
    return id;
}
bool AudioPiplineManager::cancelRequest(const RequestId& id)
{
    const auto it = m_environmentRequests.find(id);
    if (it == m_environmentRequests.end()) return false;
    RequestResult result;
    result.kind = it.value();
    result.status = RequestStatus::Cancelled;
    m_environmentRequests.erase(it);
    // 从待处理表移除后，迟到的 worker 结果会被忽略；取消通知仍异步交付。
    QTimer::singleShot(0, this, [this, id, result] { emit requestFinished(id, result); });
    return true;
}
void AudioPiplineManager::getDevices() { refreshDevices(); }

SharedPcmQueue& AudioPiplineManager::pcmQueue()
{
    if (m_pcmQueue) return *m_pcmQueue;
    m_pcmQueue = std::make_shared<SharedPcmQueue>();
    connect(m_pcmQueue.get(), &SharedPcmQueue::updated, this, [this](const CaptureId& id, quint64 revision) {
        if (id == m_captureSnapshot.captureId) emit queueUpdated(id, revision);
    });
    connect(m_pcmQueue.get(), &SharedPcmQueue::issueReported, this, [this](const AudioIssue& issue) {
        if (issue.captureId == m_captureSnapshot.captureId) emit issueReported(issue);
    });
    connect(m_pcmQueue.get(), &SharedPcmQueue::segmentFinished, this, [this](const CaptureId& id, std::uint32_t epoch) {
        // 暂停只在该片段的结束标记和所有持有块都归还后完成；旧片段的通知不能结束新片段。
        if (id != m_captureSnapshot.captureId || epoch != m_captureSnapshot.epoch ||
            m_captureSnapshot.state != CaptureState::Stopping || m_stopTarget != StopTarget::Paused || m_pendingStops)
            return;
        m_captureSnapshot.state = CaptureState::Paused;
        emit captureChanged(m_captureSnapshot);
    });
    connect(m_pcmQueue.get(), &SharedPcmQueue::drained, this, [this](const CaptureId& id, const DrainResult& result) {
        if (id != m_captureSnapshot.captureId || m_captureSnapshot.state != CaptureState::Stopping ||
            m_stopTarget != StopTarget::Idle || m_pendingStops) return;
        if (!m_queueDrainedEmitted) {
            m_queueDrainedEmitted = true;
            emit queueDrained(id, result);
        }
        // 外部槽函数可能在通知期间启动嵌套事件循环并改变会话，关闭前再次核对。
        if (id == m_captureSnapshot.captureId && m_captureSnapshot.state == CaptureState::Stopping)
            closeDrainedQueue();
    });
    return *m_pcmQueue;
}
EnvironmentSnapshot AudioPiplineManager::environment() const { return m_environment; }
CaptureSnapshot AudioPiplineManager::capture() const { return m_captureSnapshot; }
QueueInfo AudioPiplineManager::queueInfo() const { return queueSnapshot().info; }
QueueSnapshot AudioPiplineManager::queueSnapshot() const { return m_pcmQueue ? m_pcmQueue->snapshot() : QueueSnapshot{}; }

StartResult AudioPiplineManager::startCapture(const CaptureConfig &config)
{
    if (!m_captureContext->initialized()) return {false, {}, error(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"))};
    if (m_captureSnapshot.state != CaptureState::Idle) return {false, {}, error(AudioErrorCode::Busy, QStringLiteral("已有采集任务"))};
    const auto bits = config.sources.toInt();
    if (!bits || (bits & ~3) || !config.queue.slotCount || !config.queue.payloadCapacity ||
        (contains(config.sources, AudioSource::Mic) && config.mic.mode == DeviceSelectionMode::Explicit && config.mic.deviceId.isEmpty()) ||
        (contains(config.sources, AudioSource::Spk) && config.spk.mode == DeviceSelectionMode::Explicit && config.spk.deviceId.isEmpty()))
        return {false, {}, error(AudioErrorCode::InvalidArgument, QStringLiteral("采集配置无效"))};

    m_captureConfig = config;
    m_captureSnapshot = {};
    m_captureSnapshot.captureId = token();
    m_captureSnapshot.state = CaptureState::Starting;
    m_captureSnapshot.requestedSources = config.sources;
    m_resumeSources = config.sources;
    m_sourceInfos.clear();
    m_faultStops = {};
    m_pendingStops = {};
    m_captureStartedEmitted = m_captureStoppedEmitted = m_queueDrainedEmitted = false;
    m_levelsDirty = false;
    m_captureOrigin = std::chrono::steady_clock::now();
    for (auto source : {AudioSource::Mic, AudioSource::Spk}) {
        ChannelStatus channel;
        channel.source = source;
        channel.state = contains(config.sources, source) ? ChannelState::Starting : ChannelState::Disabled;
        m_captureSnapshot.channels.append(channel);
    }
    const auto id = m_captureSnapshot.captureId;
    const auto operation = ++m_operation;
    // 延后一轮事件循环执行启动，让调用方先收到受理结果和 captureId。
    QTimer::singleShot(0, this, [this, id, operation] {
        if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Starting) return;
        emit captureChanged(m_captureSnapshot);
        if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Starting) return;
        const auto created = pcmQueue().create(m_captureConfig.queue, id, m_captureOrigin);
        if (created.status != QueueStatus::Ok) {
            m_captureSnapshot.state = CaptureState::Idle;
            emit issueReported({id, false, 0, created.error});
            emit captureChanged(m_captureSnapshot);
            return;
        }
        pcmQueue().resumeEpoch(id, 0, m_captureConfig.sources);
        prepareSources();
    });
    return {true, id, {}};
}

void AudioPiplineManager::prepareSources()
{
    ensureCaptureWorkers();
    m_startingSources = m_resumeSources;
    m_pendingStartReplies = m_startingSources;
    m_startPhase = StartPhase::Preparing;
    for (auto source : {AudioSource::Mic, AudioSource::Spk}) {
        if (!contains(m_startingSources, source)) continue;
        AudioCaptureWorker::Preparation command;
        command.selection = source == AudioSource::Mic ? m_captureConfig.mic : m_captureConfig.spk;
        command.output = {m_pcmQueue, m_captureSnapshot.captureId, m_captureOrigin, m_captureConfig.queue.payloadCapacity, m_operation};
        command.resuming = m_captureStartedEmitted;
        emit prepareSourceRequested(source, command);
    }
}

void AudioPiplineManager::onPrepared(AudioCaptureWorker::Result result)
{
    const auto source = result.info.source;
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Starting ||
        m_startPhase != StartPhase::Preparing || !contains(m_pendingStartReplies, source)) return;
    publishEnvironment(result.environment);
    // environmentChanged 的槽函数可能同步取消或停止本次操作。
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Starting) return;
    m_pendingStartReplies.setFlag(source, false);
    if (result.error.code != AudioErrorCode::None) { failStartingSource(result); return; }
    for (auto it = m_sourceInfos.begin(); it != m_sourceInfos.end();)
        if (it->source == source) it = m_sourceInfos.erase(it); else ++it;
    m_sourceInfos.append(result.info);
    pcmQueue().setSourceFormat(result.captureId, source, result.info.format);
    for (auto& channel : m_captureSnapshot.channels)
        if (channel.source == source) channel.device = result.info.device;
    advanceStartup();
}

void AudioPiplineManager::onStarted(AudioCaptureWorker::Result result)
{
    const auto source = result.info.source;
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Starting ||
        m_startPhase != StartPhase::Starting || !contains(m_pendingStartReplies, source)) return;
    m_pendingStartReplies.setFlag(source, false);
    if (result.error.code != AudioErrorCode::None) { failStartingSource(result); return; }
    advanceStartup();
}

void AudioPiplineManager::onConfirmed(AudioCaptureWorker::Result result)
{
    const auto source = result.info.source;
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Starting ||
        m_startPhase != StartPhase::Confirming || !contains(m_pendingStartReplies, source)) return;
    m_pendingStartReplies.setFlag(source, false);
    if (result.error.code != AudioErrorCode::None) { failStartingSource(result); return; }
    advanceStartup();
}

void AudioPiplineManager::advanceStartup()
{
    // 准备、启动、确认均按来源汇总。确认阶段重新检查 worker 状态，
    // 避免一路慢启动期间另一路断开，却仍向外发布启动成功。
    if (m_captureSnapshot.state != CaptureState::Starting || m_pendingStartReplies) return;
    if (!m_startingSources) { stopCapture(m_captureSnapshot.captureId, StopMode::Discard); return; }
    const auto id = m_captureSnapshot.captureId;
    const auto operation = m_operation;
    if (m_startPhase == StartPhase::Preparing) {
        m_startPhase = StartPhase::Starting;
        m_pendingStartReplies = m_startingSources;
        for (auto source : {AudioSource::Mic, AudioSource::Spk})
            if (contains(m_startingSources, source)) emit startSourceRequested(source, id, operation);
        return;
    }
    if (m_startPhase == StartPhase::Starting) {
        m_startPhase = StartPhase::Confirming;
        m_pendingStartReplies = m_startingSources;
        for (auto source : {AudioSource::Mic, AudioSource::Spk})
            if (contains(m_startingSources, source)) emit confirmSourceRequested(source, id, operation);
        return;
    }
    m_startPhase = StartPhase::None;
    m_resumeSources = m_startingSources;
    m_captureSnapshot.runningSources = m_startingSources;
    m_captureSnapshot.state = CaptureState::Running;
    for (auto& channel : m_captureSnapshot.channels)
        if (contains(m_startingSources, channel.source)) channel.state = ChannelState::Running;
    emit captureChanged(m_captureSnapshot);
    // 对外信号允许同步重入；只有操作仍有效时才能发布启动并开放 PCM 写入。
    if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Running) return;
    if (!m_captureStartedEmitted) {
        m_captureStartedEmitted = true;
        CaptureInfo info{id, m_captureConfig, m_sourceInfos, queueInfo()};
        emit captureStarted(id, info);
    }
    if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Running) return;
    m_levelTimer->start();
    for (auto source : {AudioSource::Mic, AudioSource::Spk})
        if (contains(m_startingSources, source)) emit activateSourceRequested(source, id, operation);
}

void AudioPiplineManager::markSourceFailed(AudioSource source, const AudioError& problem)
{
    m_captureSnapshot.degraded = true;
    m_captureSnapshot.runningSources.setFlag(source, false);
    m_resumeSources.setFlag(source, false);
    for (auto& channel : m_captureSnapshot.channels) if (channel.source == source) {
        channel.state = ChannelState::Failed;
        channel.error = problem;
    }
}

void AudioPiplineManager::failStartingSource(const AudioCaptureWorker::Result& result)
{
    const auto source = result.info.source;
    m_pendingStartReplies.setFlag(source, false);
    m_startingSources.setFlag(source, false);
    markSourceFailed(source, result.error);
    // 首次启动必须全部成功，否则整体回滚；已发布会话恢复时可以保留其他来源。
    if (m_captureStartedEmitted) {
        m_faultStops |= source;
        pcmQueue().seal(result.captureId, AudioSources(source), StopMode::Drain);
    }
    // 在通知外部错误前先登记停机，防止槽函数重入后漏停设备或误报启动成功。
    if (!m_captureStartedEmitted || !m_startingSources) {
        stopCapture(result.captureId, StopMode::Discard);
        emit issueReported({result.captureId, false, 0, result.error});
        return;
    }
    emit stopSourceRequested(source, result.captureId, result.operation);
    emit issueReported({result.captureId, false, 0, result.error});
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Starting) return;
    advanceStartup();
}

void AudioPiplineManager::onSourceFailed(AudioCaptureWorker::Result result)
{
    if (!isCurrent(result.captureId, result.operation)) return;
    const auto source = result.info.source;
    if (m_captureSnapshot.state == CaptureState::Starting) {
        if (contains(m_startingSources, source)) failStartingSource(result);
        return;
    }
    if (m_captureSnapshot.state != CaptureState::Running || !contains(m_captureSnapshot.runningSources, source)) return;
    if (!m_captureStartedEmitted) {
        markSourceFailed(source, result.error);
        stopCapture(result.captureId, StopMode::Discard);
        emit issueReported({result.captureId, false, 0, result.error});
        return;
    }
    // 运行中单路故障先封口并保留其他来源；等该 worker 停稳后再补 StreamEnd。
    m_faultStops |= source;
    pcmQueue().seal(result.captureId, AudioSources(source), StopMode::Drain);
    markSourceFailed(source, result.error);
    emit issueReported({result.captureId, false, 0, result.error});
    if (!isCurrent(result.captureId, result.operation) || m_captureSnapshot.state != CaptureState::Running) return;
    emit captureChanged(m_captureSnapshot);
    if (isCurrent(result.captureId, result.operation) && m_captureSnapshot.state == CaptureState::Running && !m_captureSnapshot.runningSources)
        stopCapture(result.captureId, StopMode::Discard);
}

CommandResult AudioPiplineManager::pauseCapture(const CaptureId& id)
{
    if (id.isEmpty() || id != m_captureSnapshot.captureId) return {false, error(AudioErrorCode::StaleCapture, QStringLiteral("采集标识已失效"))};
    if (m_captureSnapshot.state == CaptureState::Paused ||
        (m_captureSnapshot.state == CaptureState::Stopping && m_stopTarget == StopTarget::Paused)) return {true, {}};
    if (m_captureSnapshot.state != CaptureState::Running) return {false, error(AudioErrorCode::InvalidState, QStringLiteral("当前无法暂停"))};
    m_resumeSources = m_captureSnapshot.runningSources;
    beginStop(StopTarget::Paused, StopMode::Discard);
    return {true, {}};
}

CommandResult AudioPiplineManager::resumeCapture(const CaptureId& id)
{
    if (id.isEmpty() || id != m_captureSnapshot.captureId) return {false, error(AudioErrorCode::StaleCapture, QStringLiteral("采集标识已失效"))};
    if (m_captureSnapshot.state == CaptureState::Running) return {true, {}};
    if (m_captureSnapshot.state != CaptureState::Paused) return {false, error(AudioErrorCode::InvalidState, QStringLiteral("当前无法继续"))};
    m_captureSnapshot.state = CaptureState::Starting;
    ++m_captureSnapshot.epoch;
    const auto operation = ++m_operation;
    QTimer::singleShot(0, this, [this, id, operation] {
        if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Starting) return;
        emit captureChanged(m_captureSnapshot);
        if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Starting) return;
        const auto result = pcmQueue().resumeEpoch(id, m_captureSnapshot.epoch, m_resumeSources);
        if (result.status != QueueStatus::Ok) {
            m_captureSnapshot.state = CaptureState::Paused;
            emit issueReported({id, false, 0, result.error});
            emit captureChanged(m_captureSnapshot);
            return;
        }
        prepareSources();
    });
    return {true, {}};
}

CommandResult AudioPiplineManager::stopCapture(const CaptureId& id, StopMode mode)
{
    if (id.isEmpty() || id != m_captureSnapshot.captureId) return {false, error(AudioErrorCode::StaleCapture, QStringLiteral("采集标识已失效"))};
    if (m_captureSnapshot.state == CaptureState::Idle) return {true, {}};
    if (m_captureSnapshot.state == CaptureState::Stopping && m_stopTarget == StopTarget::Idle) {
        // 已受理的 Drain 可以升级为 Discard，不能再退回 Drain。
        if (mode == StopMode::Discard && m_stopMode == StopMode::Drain) {
            m_stopMode = mode;
            if (m_pcmQueue) m_pcmQueue->seal(id, m_captureConfig.sources, mode);
        }
        if (!m_pendingStops) finishStop();
        return {true, {}};
    }
    beginStop(StopTarget::Idle, mode);
    return {true, {}};
}

void AudioPiplineManager::beginStop(StopTarget target, StopMode mode)
{
    const auto id = m_captureSnapshot.captureId;
    const auto operation = ++m_operation;
    m_startPhase = StartPhase::None;
    m_pendingStartReplies = {};
    m_stopTarget = target;
    m_stopMode = mode;
    m_pendingStops = m_micWorker ? m_captureConfig.sources : AudioSources{};
    m_captureSnapshot.state = CaptureState::Stopping;
    m_captureSnapshot.runningSources = {};
    m_levelTimer->stop();
    // 先封口阻止新 PCM，再通知 worker 停机；队列收尾必须等所有来源回执。
    if (m_pcmQueue && m_pcmQueue->snapshot().state != QueueState::Closed)
        m_pcmQueue->seal(id, m_captureConfig.sources, mode);
    emit captureChanged(m_captureSnapshot);
    if (!isCurrent(id, operation) || m_captureSnapshot.state != CaptureState::Stopping) return;
    if (!m_pendingStops) { finishStop(); return; }
    for (auto source : {AudioSource::Mic, AudioSource::Spk})
        if (contains(m_pendingStops, source)) emit stopSourceRequested(source, id, operation);
}

void AudioPiplineManager::onStopped(AudioCaptureWorker::Result result)
{
    if (!isCurrent(result.captureId, result.operation)) return;
    const auto source = result.info.source;
    // 单路故障可能在停机过程中被暂停/最终停止覆盖，仍要先补该来源的故障尾标。
    if (contains(m_faultStops, source)) {
        m_faultStops.setFlag(source, false);
        auto reason = AudioErrorCode::DeviceDisconnected;
        for (const auto& channel : m_captureSnapshot.channels)
            if (channel.source == source) reason = channel.error.code;
        pcmQueue().finishSource(result.captureId, source, reason);
    }
    if (m_captureSnapshot.state == CaptureState::Stopping && contains(m_pendingStops, source)) {
        m_pendingStops.setFlag(source, false);
        if (!m_pendingStops) finishStop();
    }
}

void AudioPiplineManager::finishStop()
{
    if (m_pendingStops || m_captureSnapshot.state != CaptureState::Stopping) return;
    const auto id = m_captureSnapshot.captureId;
    if (m_queueDrainedEmitted) { closeDrainedQueue(); return; }
    if (m_pcmQueue && m_pcmQueue->snapshot().state != QueueState::Closed) {
        // 从未发布音频的启动回滚不伪造尾标；暂停收 SegmentEnd，最终停止收 StreamEnd。
        const auto sources = m_captureStartedEmitted
            ? (m_stopTarget == StopTarget::Paused ? m_resumeSources : m_captureConfig.sources) : AudioSources{};
        const auto kind = m_stopTarget == StopTarget::Paused ? BlockKind::SegmentEnd : BlockKind::StreamEnd;
        const auto result = m_pcmQueue->finish(id, sources, kind, m_stopMode);
        if (result.status != QueueStatus::Ok) { emit issueReported({id, false, 0, result.error}); return; }
        // 先登记异步收尾再发 captureStopped，槽函数进入嵌套事件循环时队列仍能继续排空。
        if (m_stopTarget == StopTarget::Idle && !m_captureStoppedEmitted) {
            m_captureStoppedEmitted = true;
            emit captureStopped(id);
        }
        return;
    }
    if (m_stopTarget == StopTarget::Idle && !m_captureStoppedEmitted) {
        m_captureStoppedEmitted = true;
        emit captureStopped(id);
    }
    if (id != m_captureSnapshot.captureId || m_captureSnapshot.state != CaptureState::Stopping) return;
    m_captureSnapshot.state = m_stopTarget == StopTarget::Paused ? CaptureState::Paused : CaptureState::Idle;
    emit captureChanged(m_captureSnapshot);
}

void AudioPiplineManager::closeDrainedQueue()
{
    const auto result = m_pcmQueue->close();
    if (result.status != QueueStatus::Ok) {
        emit issueReported({m_captureSnapshot.captureId, false, 0, result.error});
        return;
    }
    m_captureSnapshot.state = CaptureState::Idle;
    emit captureChanged(m_captureSnapshot);
}

void AudioPiplineManager::startCapture(CaptureModel mode)
{
    CaptureConfig config;
    if (mode == MIC || mode == BOTH) config.sources |= AudioSource::Mic;
    if (mode == SPK || mode == BOTH) config.sources |= AudioSource::Spk;
    startCapture(config);
}
void AudioPiplineManager::stopCapture()
{
    if (!m_captureSnapshot.captureId.isEmpty()) stopCapture(m_captureSnapshot.captureId, StopMode::Discard);
}
