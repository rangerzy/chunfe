#include "audio_capture_worker.h"

#include "audio_capture_context.h"
#include "audio_capture_buffer.h"
#include <miniaudio.h>
#ifdef __APPLE__
#include "mac_system_capture.h"
#endif

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

using namespace AudioPipline;

namespace {
constexpr std::uint32_t captureSampleRate = 16000;
constexpr std::uint32_t captureChannels = 1;

AudioError captureError(AudioErrorCode code, const QString& message, AudioSource source)
{
    return {code, message, source};
}
}

struct AudioCaptureWorker::Impl {
    AudioCaptureWorker* owner;
    AudioSource source;
    std::shared_ptr<AudioCaptureContext> context;
    Output output;
    SourceCaptureInfo info;
    ma_device device{};
    ma_device_id deviceId{};
    bool initialized = false;
    bool started = false;
    std::atomic<bool> backendStopped{false};
    std::atomic<bool> accepting{false};
    std::atomic<bool> monitoring{false};
    std::atomic<unsigned> inFlight{0};
    std::atomic<float> peak{0};
    std::atomic<float> rms{0};
    // 命令状态和定时器只在控制线程读写；上面的原子字段还会被后端回调访问。
    CaptureId commandId;
    quint64 commandOperation = 0;
    bool preparedSent = false;
    bool preparationReady = false;
    bool startedSent = false;
    bool failureSent = false;
    bool stopPending = false;
    bool stopFinished = false;
    bool stopFailureReported = false;
    bool shuttingDown = false;
    QTimer* stopTimer = nullptr;
    QTimer* levelTimer = nullptr;
#ifdef __APPLE__
    std::unique_ptr<MacSystemCapture> mac;
#endif

    Impl(AudioCaptureWorker* worker, AudioSource inputSource, std::shared_ptr<AudioCaptureContext> sharedContext)
        : owner(worker), source(inputSource), context(std::move(sharedContext))
    {
        info.source = source;
    }

    // 等后端停稳后，用在途计数确保回调不再引用 userdata 再释放它。
    struct CallbackGuard {
        std::atomic<unsigned>& count;
        explicit CallbackGuard(std::atomic<unsigned>& callbacks) : count(callbacks)
        { count.fetch_add(1, std::memory_order_acq_rel); }
        ~CallbackGuard() { count.fetch_sub(1, std::memory_order_release); }
    };

    void waitForCallbacks() const
    {
        while (inFlight.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
    }

    void reportFailure()
    {
        if (!monitoring.exchange(false, std::memory_order_acq_rel))
            return;
        accepting.store(false, std::memory_order_release);
        // 回调只关门并携带当时的会话身份排队；销毁 worker 后排队事件会被丢弃。
        auto* receiver = owner;
        const auto id = output.captureId;
        const auto generation = output.generation;
        QMetaObject::invokeMethod(receiver, [receiver, id, generation] {
            receiver->handleFailure(id, generation);
        }, Qt::QueuedConnection);
    }

    static void receive(void* userdata, const std::int16_t* input, std::uint32_t frames,
                        std::uint32_t sampleRate, std::uint32_t channels,
                        std::uint64_t remainingInputNs)
    {
        auto* self = static_cast<Impl*>(userdata);
        CallbackGuard callback(self->inFlight);
        if (!input || !frames || sampleRate != captureSampleRate || channels != captureChannels ||
            !self->accepting.load(std::memory_order_acquire) ||
            !self->monitoring.load(std::memory_order_acquire))
            return;

        float peak = 0;
        double squares = 0;
        const auto sampleCount = std::uint64_t(frames) * channels;
        for (std::uint64_t i = 0; i < sampleCount; ++i) {
            // 转为归一化浮点数仅用于电平，音频缓冲保留 16 位 PCM。
            const float sample = float(input[i]) / 32768.0f;
            peak = std::max(peak, std::abs(sample));
            squares += double(sample) * sample;
        }
        self->peak.store(peak, std::memory_order_relaxed);
        self->rms.store(sampleCount ? float(std::sqrt(squares / sampleCount)) : 0, std::memory_order_relaxed);

        // 只显示电平的会话不保留 PCM。音频缓冲写入不等待消费者。
        auto* buffer = self->output.buffer.get();
        if (!buffer) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - self->output.origin).count();
        // macOS 一次输入可能拆成多个输出块；减去尚未处理的输入时长，避免各块时间重叠。
        const auto begin = std::max<std::int64_t>(0, elapsed - std::int64_t(remainingInputNs) -
            std::int64_t(std::uint64_t(frames) * 1000000000ULL / sampleRate));
        if (buffer->write(input, frames, begin))
            emit self->owner->pcmAvailable(self->output.captureId, self->output.generation, self->source);
    }

    static void onData(ma_device* device, void*, const void* input, ma_uint32 frames)
    {
        receive(device->pUserData, static_cast<const std::int16_t*>(input), frames,
                device->sampleRate, device->capture.channels, 0);
    }

    static void onNotification(const ma_device_notification* notification)
    {
        if (!notification || notification->type != ma_device_notification_type_stopped)
            return;
        auto* self = static_cast<Impl*>(notification->pDevice->pUserData);
        CallbackGuard callback(self->inFlight);
        self->backendStopped.store(true, std::memory_order_release);
        self->reportFailure();
    }

#ifdef __APPLE__
    static void onMacFailure(void* userdata, const char*)
    {
        auto* self = static_cast<Impl*>(userdata);
        CallbackGuard callback(self->inFlight);
        self->reportFailure();
    }
#endif

    AudioError stopBackend()
    {
        monitoring.store(false, std::memory_order_release);
        accepting.store(false, std::memory_order_release);
#ifdef __APPLE__
        if (mac) {
            QString message;
            if (!mac->stop(&message)) {
                waitForCallbacks();
                return captureError(AudioErrorCode::BackendStopFailed, message, source);
            }
            mac.reset();
        }
#endif
        if (initialized) {
            if (started && !backendStopped.load(std::memory_order_acquire)) {
                const auto status = ma_device_stop(&device);
                if (status != MA_SUCCESS) {
                    waitForCallbacks();
                    return captureError(AudioErrorCode::BackendStopFailed,
                        QStringLiteral("停止音频设备失败 (%1)").arg(int(status)), source);
                }
            }
            ma_device_uninit(&device);
        }
        waitForCallbacks();
        initialized = false;
        started = false;
        backendStopped.store(false, std::memory_order_release);
        peak.store(0, std::memory_order_relaxed);
        rms.store(0, std::memory_order_relaxed);
        return {};
    }
};

AudioCaptureWorker::AudioCaptureWorker(AudioSource source, std::shared_ptr<AudioCaptureContext> context,
                                       QObject* parent)
    : QObject(parent), d(std::make_unique<Impl>(this, source, std::move(context)))
{
}

AudioCaptureWorker::~AudioCaptureWorker()
{
    d->shuttingDown = true;
    if (d->stopTimer) d->stopTimer->stop();
    if (d->levelTimer) d->levelTimer->stop();
    const auto failure = stopCapture();
    d->waitForCallbacks();
    d->owner = nullptr;
    if (failure.code != AudioErrorCode::None) {
        // 后端停止失败时仍可能回调 userdata，故保留已关门的后端及其依赖，避免悬空访问。
        d.release();
    }
}

AudioError AudioCaptureWorker::prepare(const DeviceSelection& selection, const Output& output, bool resuming)
{
    if (!d->context || !d->context->initialized())
        return captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), d->source);
    if ((d->source != AudioSource::Mic && d->source != AudioSource::Spk) || output.captureId.isEmpty())
        return captureError(AudioErrorCode::InvalidArgument, QStringLiteral("采集配置无效"), d->source);

    std::lock_guard<std::mutex> lock(d->context->backendMutex());
    if (d->initialized
#ifdef __APPLE__
        || d->mac
#endif
    )
        return captureError(AudioErrorCode::InvalidState, QStringLiteral("采集设备尚未释放"), d->source);

    DeviceSelection resolved = selection;
    if (resuming) {
        // 暂停恢复沿用本次会话启动时选定的设备，不跟随系统默认设备变化。
        resolved.mode = DeviceSelectionMode::Explicit;
        resolved.deviceId = d->info.device.id;
    }
    SourceCaptureInfo prepared;
    prepared.source = d->source;
    const void* nativeId = d->context->resolveDevice(d->source, resolved, prepared.device);
    if (!nativeId)
        return captureError(AudioErrorCode::DeviceUnavailable, QStringLiteral("所选音频设备不可用"), d->source);

    d->deviceId = *static_cast<const ma_device_id*>(nativeId);
    d->output = output;
    d->accepting.store(false, std::memory_order_release);
    d->monitoring.store(false, std::memory_order_release);
    d->backendStopped.store(false, std::memory_order_release);

    auto fail = [&](AudioError problem) {
        const auto cleanup = d->stopBackend();
        return cleanup.code == AudioErrorCode::None ? problem : cleanup;
    };
#ifdef __APPLE__
    if (d->source == AudioSource::Spk) {
        d->mac = std::make_unique<MacSystemCapture>();
        QString message;
        if (!d->mac->prepare(prepared.device.id, &Impl::receive, d.get(), &message, &Impl::onMacFailure))
            return fail(captureError(AudioErrorCode::BackendStartFailed, message, d->source));
        prepared.format = {d->mac->sampleRate(), d->mac->channels(), SampleType::Int16, PcmLayout::Interleaved};
    } else
#endif
    {
        auto* context = static_cast<ma_context*>(d->context->backendContext());
        if (d->source == AudioSource::Spk && context->backend != ma_backend_wasapi)
            return captureError(AudioErrorCode::UnsupportedSource, QStringLiteral("系统音频采集不可用"), d->source);
        auto config = ma_device_config_init(d->source == AudioSource::Mic ? ma_device_type_capture : ma_device_type_loopback);
        config.capture.pDeviceID = &d->deviceId;
        // 请求统一的输出格式；设备不支持时由 miniaudio 在后端转换。
        config.capture.format = ma_format_s16;
        config.capture.channels = captureChannels;
        config.sampleRate = captureSampleRate;
        config.resampling.linear.lpfOrder = MA_MAX_FILTER_ORDER;
        config.dataCallback = &Impl::onData;
        config.notificationCallback = &Impl::onNotification;
        config.pUserData = d.get();
        config.wasapi.noAutoStreamRouting = MA_TRUE;
        const auto status = ma_device_init(context, &config, &d->device);
        if (status != MA_SUCCESS)
            return fail(captureError(AudioErrorCode::BackendStartFailed,
                QStringLiteral("打开音频设备失败 (%1)").arg(int(status)), d->source));
        d->initialized = true;
        prepared.format = {d->device.sampleRate, d->device.capture.channels, SampleType::Int16, PcmLayout::Interleaved};
    }
    if (prepared.format.sampleRate != captureSampleRate || prepared.format.channels != captureChannels)
        return fail(captureError(AudioErrorCode::UnsupportedFormat, QStringLiteral("设备不支持所需的采集格式"), d->source));
    d->info = prepared;
    return {};
}

AudioError AudioCaptureWorker::startCapture()
{
    if (!d->context || !d->context->initialized())
        return captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), d->source);
    std::lock_guard<std::mutex> lock(d->context->backendMutex());
    if (d->started)
        return {};
    if (!d->initialized
#ifdef __APPLE__
        && !d->mac
#endif
    )
        return captureError(AudioErrorCode::InvalidState, QStringLiteral("采集设备尚未准备"), d->source);

    // 先监听故障、后启动后端；accepting 保持关闭，直到 manager 确认所有来源。
    d->monitoring.store(true, std::memory_order_release);
#ifdef __APPLE__
    if (d->mac) {
        QString message;
        if (!d->mac->start(&message)) {
            d->monitoring.store(false, std::memory_order_release);
            return captureError(AudioErrorCode::BackendStartFailed, message, d->source);
        }
    } else
#endif
    {
        const auto status = ma_device_start(&d->device);
        if (status != MA_SUCCESS) {
            d->monitoring.store(false, std::memory_order_release);
            return captureError(AudioErrorCode::BackendStartFailed,
                QStringLiteral("启动音频设备失败 (%1)").arg(int(status)), d->source);
        }
    }
    d->started = true;
    return {};
}

AudioError AudioCaptureWorker::stopCapture()
{
    // 先关门再等共享后端锁，避免等待期间继续把回调写入队列。
    d->monitoring.store(false, std::memory_order_release);
    d->accepting.store(false, std::memory_order_release);
    if (!d->context)
        return {};
    std::lock_guard<std::mutex> lock(d->context->backendMutex());
    return d->stopBackend();
}

SourceCaptureInfo AudioCaptureWorker::info() const { return d->info; }

void AudioCaptureWorker::setAccepting(bool accepting)
{
    d->accepting.store(accepting, std::memory_order_release);
    if (accepting && !d->monitoring.load(std::memory_order_acquire))
        d->accepting.store(false, std::memory_order_release);
}

bool AudioCaptureWorker::monitoring() const { return d->monitoring.load(std::memory_order_acquire); }
AudioCaptureWorker::Level AudioCaptureWorker::level() const
{
    return {d->peak.load(std::memory_order_relaxed), d->rms.load(std::memory_order_relaxed)};
}

void AudioCaptureWorker::prepareSource(AudioSource target, Preparation preparation)
{
    if (target != d->source || d->shuttingDown)
        return;
    const auto& id = preparation.output.captureId;
    const auto operation = preparation.output.generation;
    // 重复或较旧的准备命令不能重开已处理、正在停止的设备。
    if (operation < d->commandOperation ||
        (id == d->commandId && operation == d->commandOperation &&
         (d->preparedSent || d->stopPending || d->stopFinished)))
        return;

    Result result;
    result.captureId = id;
    result.operation = operation;
    result.info = d->info;
    if (d->stopPending || d->initialized
#ifdef __APPLE__
        || d->mac
#endif
    ) {
        result.error = captureError(AudioErrorCode::Busy, QStringLiteral("采集设备尚未释放"), d->source);
        emit prepared(result);
        return;
    }

    d->commandId = id;
    d->commandOperation = operation;
    d->preparedSent = true;
    d->preparationReady = false;
    d->startedSent = false;
    d->failureSent = false;
    d->stopFinished = false;
    d->stopFailureReported = false;
    if (!d->context) {
        result.error = captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), d->source);
    } else {
        result.environment = d->context->refresh();
        if (result.environment.error.code != AudioErrorCode::None) {
            result.error = result.environment.error;
            result.error.source = d->source;
        } else {
            result.error = prepare(preparation.selection, preparation.output, preparation.resuming);
        }
    }
    d->preparationReady = result.error.code == AudioErrorCode::None;
    result.info = d->info;
    emit prepared(result);
}

void AudioCaptureWorker::startSource(AudioSource target, CaptureId id, quint64 operation)
{
    if (target != d->source || d->shuttingDown || id != d->commandId ||
        operation != d->commandOperation || d->startedSent || d->stopPending || d->stopFinished)
        return;
    d->startedSent = true;
    Result result;
    result.captureId = id;
    result.operation = operation;
    result.info = d->info;
    if (!d->preparationReady) {
        result.error = captureError(AudioErrorCode::InvalidState, QStringLiteral("采集设备尚未准备"), d->source);
    } else {
        result.error = startCapture();
        if (result.error.code == AudioErrorCode::None && !monitoring())
            result.error = captureError(AudioErrorCode::DeviceDisconnected, QStringLiteral("音频设备在启动期间断开"), d->source);
    }
    emit started(result);
}

void AudioCaptureWorker::confirmSource(AudioSource target, CaptureId id, quint64 operation)
{
    if (target != d->source || d->shuttingDown || id != d->commandId || operation != d->commandOperation)
        return;

    // 另一路后端启动可能阻塞控制线程；汇总启动回执后再次检查期间是否断线。
    Result result;
    result.captureId = id;
    result.operation = operation;
    result.info = d->info;
    if (d->failureSent || (d->startedSent && !monitoring())) {
        result.error = captureError(AudioErrorCode::DeviceDisconnected, QStringLiteral("音频设备在启动期间断开"), d->source);
    } else if (!d->startedSent || !d->started || d->stopPending || d->stopFinished) {
        result.error = captureError(AudioErrorCode::InvalidState, QStringLiteral("采集设备未处于已启动状态"), d->source);
    }
    emit confirmed(result);
}

void AudioCaptureWorker::activateSource(AudioSource target, CaptureId id, quint64 operation)
{
    if (target != d->source || d->shuttingDown || id != d->commandId ||
        operation != d->commandOperation || !d->startedSent || !d->started ||
        d->stopPending || d->stopFinished || !monitoring())
        return;
    setAccepting(true);
    if (!d->levelTimer) {
        d->levelTimer = new QTimer(this);
        d->levelTimer->setInterval(50);
        connect(d->levelTimer, &QTimer::timeout, this, &AudioCaptureWorker::publishLevel);
    }
    d->levelTimer->start();
}

void AudioCaptureWorker::stopSource(AudioSource target, CaptureId id, quint64 operation)
{
    if (target != d->source || d->shuttingDown || operation < d->commandOperation)
        return;
    if (id != d->commandId && !d->commandId.isEmpty() &&
        (!d->stopFinished || d->initialized
#ifdef __APPLE__
         || d->mac
#endif
        ))
        return;
    if (id == d->commandId && operation == d->commandOperation && (d->stopPending || d->stopFinished))
        return;

    // 较新的停止操作接管旧重试，最终回执使用最新 operation，避免暂停升级后回执过期。
    d->commandId = id;
    d->commandOperation = operation;
    d->preparationReady = false;
    d->stopPending = true;
    d->stopFinished = false;
    d->stopFailureReported = false;
    d->monitoring.store(false, std::memory_order_release);
    setAccepting(false);
    if (d->levelTimer) d->levelTimer->stop();
    if (d->stopTimer) d->stopTimer->stop();
    retryStop();
}

void AudioCaptureWorker::retryStop()
{
    if (!d->stopPending || d->shuttingDown)
        return;
    const auto error = stopCapture();
    if (error.code != AudioErrorCode::None) {
        if (!d->stopFailureReported) {
            d->stopFailureReported = true;
            emit issueReported({d->commandId, error});
        }
        if (d->shuttingDown || !d->stopPending)
            return;
        // 不阻塞控制线程；同一次停止只报告一次故障，直到后端停稳才发送 stopped。
        if (!d->stopTimer) {
            d->stopTimer = new QTimer(this);
            d->stopTimer->setSingleShot(true);
            connect(d->stopTimer, &QTimer::timeout, this, &AudioCaptureWorker::retryStop);
        }
        d->stopTimer->start(50);
        return;
    }

    d->stopPending = false;
    d->stopFinished = true;
    d->stopFailureReported = false;
    if (d->stopTimer) d->stopTimer->stop();
    Result result;
    result.captureId = d->commandId;
    result.operation = d->commandOperation;
    result.info = d->info;
    emit stopped(result);
}

void AudioCaptureWorker::handleFailure(const CaptureId& id, quint64 operation)
{
    if (d->shuttingDown || id != d->output.captureId || operation != d->output.generation)
        return;
    // 兼容直接使用 worker 的调用；manager 走命令邮箱时身份已由 prepareSource 绑定。
    if (d->commandId.isEmpty()) {
        d->commandId = id;
        d->commandOperation = operation;
    }
    if (id != d->commandId || operation != d->commandOperation || d->failureSent ||
        d->stopPending || d->stopFinished)
        return;
    d->failureSent = true;
    setAccepting(false);
    if (d->levelTimer) d->levelTimer->stop();
    Result result;
    result.captureId = id;
    result.operation = operation;
    result.info = d->info;
    result.error = captureError(AudioErrorCode::DeviceDisconnected, QStringLiteral("音频设备已断开"), d->source);
    // 先报告断线再自行停机，manager 可以先封禁该来源并记录失败。
    emit sourceFailed(result);
    if (d->shuttingDown || id != d->commandId || operation != d->commandOperation)
        return;
    emit failed(id, operation, d->source);
    if (d->shuttingDown || id != d->commandId || operation != d->commandOperation)
        return;
    stopSource(d->source, id, operation);
}

void AudioCaptureWorker::publishLevel()
{
    if (d->shuttingDown || d->stopPending || d->stopFinished)
        return;
    const auto measured = level();
    emit levelChanged(d->commandId, d->source, measured.peak, measured.rms);
}

void AudioCaptureWorker::shutdown()
{
    d->shuttingDown = true;
    d->stopPending = false;
    if (d->stopTimer) d->stopTimer->stop();
    if (d->levelTimer) d->levelTimer->stop();
    d->monitoring.store(false, std::memory_order_release);
    setAccepting(false);
    stopCapture();
}

void AudioCaptureWorker::refreshEnvironment(RequestId id)
{
    if (d->source != AudioSource::Mic || d->shuttingDown)
        return;
    RequestResult result;
    result.kind = RequestKind::RefreshDevices;
    if (d->context) {
        result.environment = d->context->refresh();
        result.error = result.environment.error;
    } else {
        result.error = captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), AudioSource::None);
    }
    result.status = result.error.code == AudioErrorCode::None ? RequestStatus::Completed : RequestStatus::Failed;
    emit environmentRequestFinished(id, result);
}

void AudioCaptureWorker::checkAvailability(RequestId id, CaptureConfig config)
{
    if (d->source != AudioSource::Mic || d->shuttingDown)
        return;
    RequestResult result;
    result.kind = RequestKind::CheckAvailability;
    if (d->context) {
        result.environment = d->context->snapshot();
        result.availability = d->context->checkAvailability(config);
        result.status = RequestStatus::Completed;
    } else {
        result.error = captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), AudioSource::None);
    }
    emit environmentRequestFinished(id, result);
}

void AudioCaptureWorker::requestPermissions(RequestId id, AudioSources sources)
{
    if (d->source != AudioSource::Mic || d->shuttingDown)
        return;
    if (!d->context) {
        RequestResult result;
        result.kind = RequestKind::RequestPermissions;
        result.error = captureError(AudioErrorCode::BackendStartFailed, QStringLiteral("音频上下文不可用"), AudioSource::None);
        emit environmentRequestFinished(id, result);
        return;
    }
    // 系统权限回调可能晚于 worker 销毁或运行在其他线程，回执须切回控制线程。
    QPointer<AudioCaptureWorker> guard(this);
    d->context->requestPermissions(sources, [guard, id] {
        if (!guard)
            return;
        QMetaObject::invokeMethod(guard, [guard, id] {
            if (!guard || guard->d->shuttingDown)
                return;
            RequestResult result;
            result.kind = RequestKind::RequestPermissions;
            result.status = RequestStatus::Completed;
            result.environment = guard->d->context->snapshot();
            result.permissions = result.environment.permissions;
            emit guard->environmentRequestFinished(id, result);
        }, Qt::QueuedConnection);
    });
}
