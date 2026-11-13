#include "offline_transcription_controller.h"
#include <offline_voice_activity.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <utility>

using namespace AudioPipline;
namespace {
Q_STATIC_LOGGING_CATEGORY(offlineAsrLog, "chunfe.asr.offline")

int idx(int source) { return source == 1 ? 0 : 1; }
constexpr int PcmSampleRate = 16000;
constexpr int PcmBytesPerSecond = PcmSampleRate * 2;
constexpr int MaxPendingBytes = PcmBytesPerSecond * 5;

const char *sourceName(AudioSource source)
{
    if (source == AudioSource::Mic) return "MIC";
    if (source == AudioSource::Spk) return "SPK";
    return "unknown";
}

double pcmMilliseconds(qsizetype bytes)
{
    return bytes * 1000.0 / PcmBytesPerSecond;
}
}
struct OfflineTranscriptionController::Worker { OfflineTranscriptionEngine engine; };

QString OfflineTranscriptionController::bundledRuntimePath(const QString &applicationDir)
{
    const QDir directory(applicationDir);
#if defined(Q_OS_MACOS)
    // A bundle always loads from Frameworks. Command-line test executables use
    // the runtime deployed beside them; neither path consults user settings.
    const QString relativePath = directory.dirName() == QStringLiteral("MacOS")
        ? QStringLiteral("../Frameworks/libaudiocpp.dylib") : QStringLiteral("libaudiocpp.dylib");
#elif defined(Q_OS_WIN)
    const QString relativePath = QStringLiteral("audiocpp.dll");
#else
    const QString relativePath = QFileInfo::exists(directory.filePath(QStringLiteral("libaudiocpp.so")))
        ? QStringLiteral("libaudiocpp.so") : QString::fromUtf8(CHUNFE_AUDIOCPP_INSTALL_RELATIVE_PATH);
#endif
    return QDir::cleanPath(directory.absoluteFilePath(relativePath));
}

OfflineTranscriptionController::OfflineTranscriptionController(AudioPiplineManager &manager, QObject *parent)
    : QObject(parent), m_manager(manager), m_workerContext(new QObject), m_worker(std::make_shared<Worker>())
{
    QSettings settings;
    m_runtimePath = bundledRuntimePath(QCoreApplication::applicationDirPath());
    const auto runtimeDir = QFileInfo(m_runtimePath).absoluteDir();
    const auto vadFile = QStringLiteral("silero_vad_16k.safetensors");
#if defined(Q_OS_MACOS)
    m_vadModelPath = runtimeDir.absoluteFilePath(runtimeDir.dirName() == QStringLiteral("Frameworks")
        ? QStringLiteral("../Resources/offline-transcription/") + vadFile : vadFile);
#else
    m_vadModelPath = runtimeDir.absoluteFilePath(vadFile);
#endif
    m_modelPath = settings.value(QStringLiteral("offlineTranscription/modelPath")).toString();
    m_workerContext->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_workerContext, &QObject::deleteLater);
    m_thread.start();
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &OfflineTranscriptionController::changed);
    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, [this] {
        if (m_status != QStringLiteral("stopping")) return;
        qCWarning(offlineAsrLog).nospace().noquote()
            << "stop-timeout capture=" << m_captureId << " elapsed=" << elapsedText()
            << " mic_capsule=" << m_lineId[0] << " spk_capsule=" << m_lineId[1]
            << " mic_outstanding_ms=" << pcmMilliseconds(m_pendingBytes[0])
            << " spk_outstanding_ms=" << pcmMilliseconds(m_pendingBytes[1])
            << " mic_pending_finishes=" << m_finishingBySource[0]
            << " spk_pending_finishes=" << m_finishingBySource[1];
        ++m_generation;
        invalidateSource(1); invalidateSource(2); m_finishing = 0;
        QMetaObject::invokeMethod(m_workerContext, [worker=m_worker] { worker->engine.reset(1); worker->engine.reset(2); });
        markIncomplete();
        m_status = QStringLiteral("stopped");
        m_error = tr("部分转写尚未完成，正在后台清理");
        m_tick.stop();
        emit changed();
    });
    connect(&manager, &AudioPiplineManager::pcmReady, this, &OfflineTranscriptionController::consumeAudio);
    connect(&manager, &AudioPiplineManager::pcmEnded, this, &OfflineTranscriptionController::endAudio);
    connect(&manager, &AudioPiplineManager::captureChanged, this, &OfflineTranscriptionController::onCapture);
    connect(&manager, &AudioPiplineManager::environmentChanged, this, [this](const EnvironmentSnapshot &) { refreshNames(); });
    connect(&manager, &AudioPiplineManager::requestFinished, this, [this](const RequestId &id, const RequestResult &result) {
        if (id != m_permissionRequest || id.isEmpty()) return;
        m_permissionRequest.clear();
        if (!m_opened || m_status != QStringLiteral("starting")) return;
        if (result.status != RequestStatus::Completed) {
            m_sourceFailed[0] = true;
            m_micState = QStringLiteral("error");
            m_error = result.error.message.isEmpty() ? tr("麦克风权限不可用") : result.error.message;
        } else {
            for (const auto &permission : result.permissions) {
                if (permission.source == AudioSource::Mic && (permission.state == PermissionState::Denied || permission.state == PermissionState::Restricted)) {
                    m_sourceFailed[0] = true; m_micState = QStringLiteral("error"); m_error = tr("麦克风权限未授权");
                }
            }
        }
        beginCapture();
    });
    connect(&manager, &AudioPiplineManager::issueReported, this, [this](const AudioIssue &issue) {
        if (issue.captureId != m_captureId || m_captureId.isEmpty()) return;
        if (issue.error.source == AudioSource::Mic || issue.error.source == AudioSource::Spk)
            failSource(static_cast<int>(issue.error.source), issue.error.message);
    });
}

OfflineTranscriptionController::~OfflineTranscriptionController()
{
    close();
    // The engine cannot interrupt native inference. Destroy it on its owning thread.
    QMetaObject::invokeMethod(m_workerContext, [worker = m_worker] { worker->engine.unload(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
}

QString OfflineTranscriptionController::elapsedText() const
{
    const auto seconds = m_frozenElapsedMs >= 0 ? m_frozenElapsedMs / 1000 : (m_elapsed.isValid() ? m_elapsed.elapsed() / 1000 : 0);
    return QStringLiteral("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
void OfflineTranscriptionController::saveModelPath(const QString &model)
{
    const bool changedPaths = m_modelPath != model.trimmed();
    m_modelPath = model.trimmed();
    if (changedPaths && m_engineStatus == QStringLiteral("ready"))
        m_engineError = tr("路径已保存，当前引擎仍使用旧配置；请卸载后重新加载");
    QSettings s;
    s.remove(QStringLiteral("offlineTranscription/runtimePath"));
    s.setValue(QStringLiteral("offlineTranscription/modelPath"), m_modelPath);
    s.sync();
    if (s.status() != QSettings::NoError)
        m_engineError = tr("无法保存路径配置，请检查配置目录的访问权限");
    else if (!(changedPaths && m_engineStatus == QStringLiteral("ready")))
        m_engineError.clear();
    emit changed();
}
void OfflineTranscriptionController::loadEngine()
{
    if (m_engineStatus == QStringLiteral("starting") || m_opened) return;
    if (!QFileInfo(m_modelPath).isAbsolute() || !QFileInfo(m_modelPath).isFile()) {
        m_engineStatus = QStringLiteral("error"); m_engineError = tr("请选择有效的模型文件绝对路径"); emit changed(); return;
    }
    if (!QFileInfo(m_runtimePath).isFile()) {
        m_engineStatus = QStringLiteral("error"); m_engineError = tr("应用内置转写引擎缺失，请重新安装应用"); emit changed(); return;
    }
    if (!QFileInfo(m_vadModelPath).isFile()) {
        m_engineStatus = QStringLiteral("error"); m_engineError = tr("应用内置语音检测模型缺失，请重新安装应用"); emit changed(); return;
    }
    m_engineStatus = QStringLiteral("starting"); m_engineError.clear(); emit changed();
    const auto runtime = m_runtimePath, model = m_modelPath, vadModel = m_vadModelPath;
    QMetaObject::invokeMethod(m_workerContext, [this, worker = m_worker, runtime, model, vadModel] {
        QString error;
        auto vad = std::make_unique<OfflineVoiceActivity>();
        const bool ok = vad->load(runtime, vadModel, &error) && worker->engine.load(runtime, model, &error);
        if (!ok) {
            vad.reset();
            worker->engine.unload();
        }
        // VAD has exclusive ownership on the capture delivery thread from here.
        QMetaObject::invokeMethod(this, [this, ok, error, vad = std::move(vad)]() mutable {
            m_vad = std::move(vad);
            m_engineStatus = ok ? QStringLiteral("ready") : QStringLiteral("error"); m_engineError = error; emit changed();
        });
    });
}
void OfflineTranscriptionController::unloadEngine()
{
    if (m_opened || m_engineStatus == QStringLiteral("starting")) return;
    m_engineStatus = QStringLiteral("starting"); emit changed();
    m_vad.reset();
    QMetaObject::invokeMethod(m_workerContext, [this, worker = m_worker] {
        worker->engine.unload();
        QMetaObject::invokeMethod(this, [this] { m_engineStatus = QStringLiteral("stopped"); m_engineError.clear(); emit changed(); });
    });
}
void OfflineTranscriptionController::refreshNames()
{
    m_micName.clear(); m_spkName.clear();
    for (const auto &d : m_manager.environment().devices) {
        if (d.id == m_micId && d.source == AudioSource::Mic) m_micName = d.name;
        if (d.id == m_spkId && d.source == AudioSource::Spk) m_spkName = d.name;
    }
    if (m_micName.isEmpty()) m_micName = m_micId;
    if (m_spkName.isEmpty()) m_spkName = m_spkId;
    emit changed();
}
void OfflineTranscriptionController::setDevices(const QString &mic, const QString &spk)
{
    if (m_micId == mic && m_spkId == spk) return;
    if (m_opened && (m_status == QStringLiteral("running") || m_status == QStringLiteral("starting"))) { m_error = tr("设备已变更，请重新测试"); stop(); }
    m_micId = mic; m_spkId = spk; refreshNames();
}
void OfflineTranscriptionController::open()
{
    if (m_opened || m_engineStatus != QStringLiteral("ready")) return;
    m_opened = true; emit openedChanged(); startTest();
}
void OfflineTranscriptionController::restart()
{
    if (!m_opened) return;
    invalidateSource(1); invalidateSource(2);
    m_restartPending = true;
    if (!m_captureId.isEmpty()) { stopCapture(StopMode::Discard); return; }
    startTest();
}
void OfflineTranscriptionController::startTest()
{
    if (!m_opened || m_engineStatus != QStringLiteral("ready")) return;
    if (m_manager.capture().state != CaptureState::Idle) { m_status = QStringLiteral("error"); m_error = tr("音频设备正被使用"); emit changed(); return; }
    m_restartPending = false; ++m_generation;
    invalidateSource(1); invalidateSource(2);
    m_lines.clear(); m_pendingText[0].clear(); m_pendingText[1].clear(); m_lineId[0] = m_lineId[1] = -1;
    m_sourceFailed[0] = m_sourceFailed[1] = false;
    m_sourceEnded[0] = m_sourceEnded[1] = false;
    m_frozenElapsedMs = -1;
    m_captureEnded = false; m_finishing = 0; m_attemptedSources = 0; m_startedCapture = false; m_error.clear(); m_status = QStringLiteral("starting");
    m_micState = m_micId.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("starting");
    m_spkState = m_spkId.isEmpty() ? QStringLiteral("stopped") : QStringLiteral("starting");
    m_elapsed.start(); m_tick.start(); emit changed();
    const auto generation = m_generation;
    if (m_micId.isEmpty() && m_spkId.isEmpty()) { m_status = QStringLiteral("error"); m_error = tr("请选择音频设备"); emit changed(); return; }
    QString vadError;
    if (!m_micId.isEmpty() && !m_vad->start(1, &vadError)) failSource(1, vadError);
    if (!m_spkId.isEmpty() && !m_vad->start(2, &vadError)) failSource(2, vadError);
    const bool mic = !m_micId.isEmpty() && !m_sourceFailed[0];
    const bool spk = !m_spkId.isEmpty() && !m_sourceFailed[1];
    if (!mic && !spk) { m_status = QStringLiteral("error"); emit changed(); return; }
    const auto micCancelled = m_cancelled[0], spkCancelled = m_cancelled[1];
    QMetaObject::invokeMethod(m_workerContext, [this, worker=m_worker, generation, mic, spk, micCancelled, spkCancelled] {
        if (micCancelled->load() && spkCancelled->load()) return;
        QString micError, spkError;
        const bool m = !mic || (!micCancelled->load() && worker->engine.start(1, &micError));
        const bool s = !spk || (!spkCancelled->load() && worker->engine.start(2, &spkError));
        QMetaObject::invokeMethod(this, [this, generation, mic, spk, m, s, micError, spkError] {
            if (generation != m_generation || !m_opened || m_restartPending || m_status != QStringLiteral("starting")) return;
            if (mic && !m) failSource(1, micError);
            if (spk && !s) failSource(2, spkError);
            if ((!mic || !m) && (!spk || !s)) { m_status = QStringLiteral("error"); emit changed(); return; }
            if (mic && m) { m_permissionRequest = m_manager.requestPermissions(AudioSource::Mic); emit changed(); }
            else beginCapture();
        });
    });
}
void OfflineTranscriptionController::beginCapture()
{
    if (!m_opened || m_status != QStringLiteral("starting")) return;
    CaptureConfig c;
    if (!m_micId.isEmpty() && !m_sourceFailed[0]) { c.sources |= AudioSource::Mic; c.mic = {DeviceSelectionMode::Explicit, m_micId}; }
    if (!m_spkId.isEmpty() && !m_sourceFailed[1]) { c.sources |= AudioSource::Spk; c.spk = {DeviceSelectionMode::Explicit, m_spkId}; }
    c.deliverPcm = true;
    if (!c.sources) { m_status = QStringLiteral("error"); m_error = tr("没有可用的音频来源"); emit changed(); return; }
    m_attemptedSources = c.sources.toInt();
    auto result = m_manager.startCapture(c);
    if (!result.accepted) { m_status = QStringLiteral("error"); m_error = result.error.message; emit changed(); return; }
    m_captureEpoch = 0;
    m_sourceEnded[0] = m_sourceEnded[1] = false;
    m_captureId = result.captureId;
}
void OfflineTranscriptionController::onCapture(const CaptureSnapshot &snapshot)
{
    if (m_captureId.isEmpty() || snapshot.captureId != m_captureId || snapshot.epoch < m_captureEpoch) return;
    if (snapshot.epoch != m_captureEpoch) {
        m_captureEpoch = snapshot.epoch;
        m_sourceEnded[0] = m_sourceEnded[1] = false;
    }
    for (const auto &c : snapshot.channels) {
        const int source = static_cast<int>(c.source);
        if (source != 1 && source != 2) continue;
        auto &state = source == 1 ? m_micState : m_spkState;
        auto &level = source == 1 ? m_micLevel : m_spkLevel;
        if (c.state == ChannelState::Failed) { failSource(source, c.error.message); }
        else if (c.state == ChannelState::Running && !m_sourceFailed[idx(source)]) {
            level = std::clamp<qreal>(c.peak, 0, 1); state = c.hasSignal ? QStringLiteral("receiving") : QStringLiteral("waiting");
        }
    }
    if (snapshot.state == CaptureState::Running && m_status == QStringLiteral("starting")) { m_startedCapture = true; m_status = QStringLiteral("running"); }
    if (snapshot.state == CaptureState::Idle) {
        m_captureId.clear(); m_captureEnded = true;
        if (m_restartPending && m_opened) { QTimer::singleShot(0, this, [this] { if (m_restartPending && m_opened) startTest(); }); }
        else if (m_status == QStringLiteral("stopping") && m_finishing == 0) { m_status = QStringLiteral("stopped"); m_stopTimer.stop(); m_tick.stop(); }
        else if (m_status == QStringLiteral("starting") && !m_startedCapture && m_attemptedSources == 3 && (m_sourceFailed[0] != m_sourceFailed[1])) {
            const auto generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] { if (m_opened && m_status == QStringLiteral("starting") && generation == m_generation) beginCapture(); });
        } else if (m_status == QStringLiteral("starting")) { m_status = QStringLiteral("error"); if (m_error.isEmpty()) m_error = tr("音频采集启动失败"); }
    }
    emit changed();
}
void OfflineTranscriptionController::consumeAudio(const CaptureId &id, std::uint32_t captureEpoch, AudioSource audioSource,
                                                  const CapturedAudio &audio)
{
    if (!acceptsCaptureAudio(id, captureEpoch, audioSource) || audio.pcm.isEmpty()) return;
    const int source = static_cast<int>(audioSource);
    const int index = idx(source);
    if (audio.droppedFramesBefore) {
        qCWarning(offlineAsrLog).nospace().noquote()
            << "capture-gap capture=" << id << " source=" << sourceName(audioSource)
            << " elapsed=" << elapsedText() << " capsule=" << m_lineId[index]
            << " next_source_sequence=" << audio.sequence << " next_capture_ns=" << audio.timestampNs
            << " dropped_frames=" << audio.droppedFramesBefore << " sample_rate=" << PcmSampleRate
            << " dropped_ms=" << audio.droppedFramesBefore * 1000.0 / PcmSampleRate
            << " outstanding_ms=" << pcmMilliseconds(m_pendingBytes[index]);
        m_error = tr("部分音频未处理");
        // Preserve valid PCM before the gap. Its final and the new PCM go
        // through the same queue, so a discontinuity never joins two phrases.
        submitAudio(source, m_vad->takeTail(source), /*drainVadTail=*/true);
        finishStream(source, m_generation, true, /*incomplete=*/true);
        QString vadError;
        if (!m_vad->start(source, &vadError)) { failSource(source, vadError); return; }
    }
    QList<OfflineVoiceActivity::Batch> batches;
    QString vadError;
    if (!m_vad->push(source, audio.pcm, &batches, &vadError)) {
        failSource(source, vadError);
        return;
    }
    for (const auto &batch : batches) {
        submitAudio(source, batch.pcm);
        if (batch.final && !m_sourceFailed[index]) {
            qCDebug(offlineAsrLog).nospace().noquote()
                << "vad-speech-end capture=" << id << " source=" << sourceName(audioSource)
                << " elapsed=" << elapsedText() << " endpoint_sample=" << batch.endpointSample
                << " outstanding_ms=" << pcmMilliseconds(m_pendingBytes[index]);
            // The same worker queue already has this utterance's last PCM.
            // Queue its finish before submitting any PCM for the next one.
            finishStream(source, m_generation, true);
        }
    }
}
void OfflineTranscriptionController::endAudio(const CaptureId &id, std::uint32_t epoch, AudioSource audioSource,
                                              std::uint64_t trailingDroppedFrames)
{
    if (!acceptsCaptureAudio(id, epoch, audioSource) || m_status == QStringLiteral("starting")) return;
    const int source = static_cast<int>(audioSource);
    const int index = idx(source);
    m_sourceEnded[index] = true;
    if (trailingDroppedFrames) {
        qCWarning(offlineAsrLog).nospace().noquote()
            << "capture-tail-gap capture=" << id << " source=" << sourceName(audioSource)
            << " elapsed=" << elapsedText() << " capsule=" << m_lineId[index]
            << " dropped_frames=" << trailingDroppedFrames << " sample_rate=" << PcmSampleRate
            << " dropped_ms=" << trailingDroppedFrames * 1000.0 / PcmSampleRate
            << " outstanding_ms=" << pcmMilliseconds(m_pendingBytes[index]);
        m_error = tr("部分音频未处理");
    }
    finishStream(source, m_generation, false, trailingDroppedFrames != 0);
}
void OfflineTranscriptionController::invalidateSource(int source)
{
    const int index = idx(source);
    m_cancelled[index]->store(true);
    m_cancelled[index] = std::make_shared<std::atomic_bool>(false);
    ++m_sourceEpoch[index];
    m_pendingBytes[index] = 0;
    m_finishing = std::max(0, m_finishing - m_finishingBySource[index]);
    m_finishingBySource[index] = 0;
}
void OfflineTranscriptionController::submitAudio(int source, QByteArray pcm, bool drainVadTail)
{
    const int index = idx(source);
    if (pcm.isEmpty() || !acceptsAudio() || m_sourceFailed[index]) return;
    // A drain adds only the bounded PCM already retained by VAD. Count it
    // toward pending bytes so later live submissions still honor the limit.
    if (m_pendingBytes[index] + pcm.size() > MaxPendingBytes &&
        (!drainVadTail || m_pendingBytes[index] > MaxPendingBytes)) {
        qCWarning(offlineAsrLog).nospace().noquote()
            << "inference-backlog capture=" << m_captureId << " source=" << sourceName(static_cast<AudioSource>(source))
            << " elapsed=" << elapsedText() << " outstanding_ms=" << pcmMilliseconds(m_pendingBytes[index])
            << " rejected_ms=" << pcmMilliseconds(pcm.size());
        failSource(source, tr("识别速度不足，部分音频未处理"));
        return;
    }
    m_pendingBytes[index] += pcm.size();
    const auto generation = m_generation, epoch = m_sourceEpoch[index];
    const auto cancelled = m_cancelled[index];
    // A large capture block or a preserved pause can span several windows.
    // Submit at most one window per job so cancellation and text assembly
    // still run at each window boundary.
    for (qsizetype offset = 0; offset < pcm.size(); offset += OfflineTranscriptionEngine::WindowBytes) {
        auto chunk = pcm.mid(offset, OfflineTranscriptionEngine::WindowBytes);
        QMetaObject::invokeMethod(m_workerContext, [this, worker=m_worker, generation, epoch, source, chunk=std::move(chunk), cancelled] {
            if (cancelled->load()) return;
            QElapsedTimer timer; timer.start();
            OfflineTranscriptionEngine::Result result;
            QString error;
            const bool ok = worker->engine.push(source, chunk, &result, &error);
            if (!ok) cancelled->store(true);
            const auto elapsed = timer.nsecsElapsed() / 1000;
            const int bytes = chunk.size();
            QMetaObject::invokeMethod(this, [this, generation, epoch, source, bytes, ok, result = std::move(result), error, elapsed] {
                const int index = idx(source);
                if (generation != m_generation || epoch != m_sourceEpoch[index]) return;
                m_pendingBytes[index] = std::max(0, m_pendingBytes[index] - bytes);
                emit inferenceCompleted(source, elapsed, bytes);
                if (m_sourceFailed[index]) return;
                if (!ok) failSource(source, error);
                else if (result.updated) {
                    applyResult(source, result, /*isFinal=*/false);
                    emit changed();
                }
            });
        });
    }
}
bool OfflineTranscriptionController::acceptsAudio() const
{
    return m_opened && !m_restartPending && (m_status == QStringLiteral("starting") ||
        m_status == QStringLiteral("running") || m_status == QStringLiteral("stopping"));
}
bool OfflineTranscriptionController::acceptsCaptureAudio(const CaptureId &id, std::uint32_t epoch, AudioSource source) const
{
    if (id.isEmpty() || id != m_captureId || epoch != m_captureEpoch || !acceptsAudio()) return false;
    if (source != AudioSource::Mic && source != AudioSource::Spk) return false;
    const int index = idx(static_cast<int>(source));
    return !m_sourceFailed[index] && !m_sourceEnded[index];
}
void OfflineTranscriptionController::finishStream(int source, quint64 generation, bool restartStream, bool incomplete)
{
    if (!acceptsAudio() || m_sourceFailed[idx(source)]) return;
    if (!restartStream) submitAudio(source, m_vad->takeTail(source), /*drainVadTail=*/true);
    if (m_sourceFailed[idx(source)]) return;
    ++m_finishing;
    ++m_finishingBySource[idx(source)];
    const auto epoch = m_sourceEpoch[idx(source)];
    const auto cancelled = m_cancelled[idx(source)];
    QMetaObject::invokeMethod(m_workerContext, [this, worker=m_worker, source, generation, epoch, restartStream, incomplete, cancelled] {
        if (cancelled->load()) return;
        QElapsedTimer timer; timer.start();
        OfflineTranscriptionEngine::Result result;
        QString error;
        bool ok = worker->engine.finish(source, &result, &error);
        if (restartStream && ok && !cancelled->load()) ok = worker->engine.start(source, &error);
        if (!ok) cancelled->store(true);
        const auto elapsed = timer.nsecsElapsed() / 1000;
        QMetaObject::invokeMethod(this, [this, source, generation, epoch, restartStream, incomplete, ok, result = std::move(result), error, elapsed] {
            if (generation != m_generation || epoch != m_sourceEpoch[idx(source)]) return;
            m_finishing = std::max(0, m_finishing - 1);
            m_finishingBySource[idx(source)] = std::max(0, m_finishingBySource[idx(source)] - 1);
            if (m_sourceFailed[idx(source)]) {
                if (m_status == QStringLiteral("stopping") && m_captureEnded && m_finishing == 0) {
                    m_status = QStringLiteral("stopped"); m_stopTimer.stop(); m_tick.stop(); emit changed();
                }
                return;
            }
            emit inferenceCompleted(source, elapsed, 0);
            if (!ok) failSource(source, error);
            else applyResult(source, result, /*isFinal=*/true, incomplete);
            if (!restartStream && m_status == QStringLiteral("stopping") && m_captureEnded && m_finishing == 0) {
                m_status = QStringLiteral("stopped"); m_stopTimer.stop(); m_tick.stop();
            }
            emit changed();
        });
    });
}
void OfflineTranscriptionController::applyResult(int source, const OfflineTranscriptionEngine::Result &result,
                                                  bool isFinal, bool incomplete)
{
    auto &pending = m_pendingText[idx(source)];
    if (result.updated) {
        for (const auto &sentence : result.sentences) {
            pending = sentence;
            finishSentence(source);
        }
        pending = result.draft;
        updateLine(source);
    }
    if (isFinal) finishSentence(source, incomplete);
}
void OfflineTranscriptionController::updateLine(int source)
{
    const int i = idx(source);
    const QString text = m_pendingText[i].trimmed();
    if (text.isEmpty()) {
        if (m_lineId[i] >= 0) {
            m_lines.removeIf([id = m_lineId[i]](const QVariant &entry) {
                return entry.toMap().value(QStringLiteral("id")).toLongLong() == id;
            });
            m_lineId[i] = -1;
        }
        return;
    }
    if (m_lineId[i] < 0) {
        m_lineId[i] = ++m_sequence;
        m_lines.append(QVariantMap{{QStringLiteral("id"), m_lineId[i]}, {QStringLiteral("source"), source == 1 ? QStringLiteral("mic") : QStringLiteral("spk")},
                                   {QStringLiteral("time"), elapsedText()}, {QStringLiteral("text"), text}, {QStringLiteral("final"), false}, {QStringLiteral("incomplete"), false}});
    } else {
        for (auto &item : m_lines) {
            auto map = item.toMap();
            if (map.value(QStringLiteral("id")).toLongLong() == m_lineId[i]) { map[QStringLiteral("text")] = text; item = map; break; }
        }
    }
    while (m_lines.size() > 40) {
        int remove = -1;
        for (int n=0; n<m_lines.size(); ++n) if (m_lines[n].toMap().value(QStringLiteral("final")).toBool() || m_lines[n].toMap().value(QStringLiteral("incomplete")).toBool()) { remove=n; break; }
        if (remove < 0) break;
        m_lines.removeAt(remove);
    }
}
void OfflineTranscriptionController::finishSentence(int source, bool incomplete)
{
    const int i = idx(source);
    updateLine(source);
    const qint64 id = m_lineId[i];
    if (id >= 0) {
        for (auto &item : m_lines) {
            auto map = item.toMap();
            if (map.value(QStringLiteral("id")).toLongLong() == id) {
                map[QStringLiteral("final")] = !incomplete; map[QStringLiteral("incomplete")] = incomplete; item = map; break;
            }
        }
    }
    m_lineId[i] = -1; m_pendingText[i].clear();
}
void OfflineTranscriptionController::markIncomplete() { finishSentence(1, true); finishSentence(2, true); }
void OfflineTranscriptionController::failSource(int source, const QString &message)
{
    const int i = idx(source);
    if (!m_sourceFailed[i])
        qCWarning(offlineAsrLog).nospace().noquote()
            << "source-failure capture=" << m_captureId << " source=" << sourceName(static_cast<AudioSource>(source))
            << " elapsed=" << elapsedText() << " capsule=" << m_lineId[i]
            << " outstanding_ms=" << pcmMilliseconds(m_pendingBytes[i]) << " detail=" << message;
    m_sourceFailed[i] = true;
    invalidateSource(source);
    QMetaObject::invokeMethod(m_workerContext, [worker=m_worker, source] { worker->engine.reset(source); });
    finishSentence(source, true);
    (source == 1 ? m_micState : m_spkState) = QStringLiteral("error");
    m_error = message.isEmpty() ? tr("音频来源或识别失败") : message;
    if (m_status == QStringLiteral("stopping") && m_captureEnded && m_finishing == 0) {
        m_status = QStringLiteral("stopped"); m_stopTimer.stop(); m_tick.stop();
    }
    emit changed();
}
void OfflineTranscriptionController::stopCapture(StopMode mode)
{
    if (!m_permissionRequest.isEmpty()) { m_manager.cancelRequest(m_permissionRequest); m_permissionRequest.clear(); }
    if (!m_captureId.isEmpty()) {
        auto result = m_manager.stopCapture(m_captureId, mode);
        if (!result.accepted) { m_error = result.error.message; emit changed(); }
    } else if (m_status == QStringLiteral("stopping")) {
        m_status = QStringLiteral("stopped"); m_tick.stop(); m_stopTimer.stop();
    }
}
void OfflineTranscriptionController::stop()
{
    if (!m_opened || (m_status != QStringLiteral("running") && m_status != QStringLiteral("starting"))) return;
    m_frozenElapsedMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    const bool starting = m_status == QStringLiteral("starting");
    m_status = QStringLiteral("stopping"); m_stopTimer.start(5000);
    if (starting && m_captureId.isEmpty()) { invalidateSource(1); invalidateSource(2); }
    stopCapture(StopMode::Drain); emit changed();
}
void OfflineTranscriptionController::close()
{
    if (!m_opened && m_captureId.isEmpty()) return;
    ++m_generation; m_opened = false; m_restartPending = false; m_stopTimer.stop(); m_tick.stop();
    invalidateSource(1); invalidateSource(2); m_finishing = 0;
    stopCapture(StopMode::Discard);
    m_status = QStringLiteral("idle"); m_lines.clear(); m_pendingText[0].clear(); m_pendingText[1].clear();
    m_lineId[0] = m_lineId[1] = -1; m_micLevel = m_spkLevel = 0;
    QMetaObject::invokeMethod(m_workerContext, [worker=m_worker] { worker->engine.reset(1); worker->engine.reset(2); });
    emit openedChanged(); emit changed();
}
