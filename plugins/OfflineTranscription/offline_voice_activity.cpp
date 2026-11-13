#include "offline_voice_activity.h"
#include "vendor/audio.cpp/audiocpp.h"

#include <QFileInfo>
#include <QLibrary>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <utility>

namespace {
constexpr int SampleRate = 16000;
constexpr int FrameSamples = 512;
constexpr qsizetype FrameBytes = FrameSamples * 2;
constexpr qsizetype PreRollBytes = SampleRate * 2 * 300 / 1000;
constexpr int EndpointDelayMs = 800;
constexpr int TailMs = 200;
constexpr qint64 TailSamples = SampleRate * TailMs / 1000;
// VAD reports the end retrospectively. Hold the undecided PCM through one
// additional frame of detection latency, rounded up to a whole frame.
constexpr qsizetype HoldFrames = (SampleRate * (EndpointDelayMs - TailMs) / 1000 + FrameSamples - 1) / FrameSamples + 1;
constexpr qsizetype HoldBytes = HoldFrames * FrameBytes;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

struct OfflineVoiceActivity::Impl {
    QLibrary library;
#define FUNCTION(name) decltype(&audiocpp_##name) name = nullptr
    FUNCTION(last_error);
    FUNCTION(registry_create); FUNCTION(registry_free);
    FUNCTION(model_load); FUNCTION(model_free); FUNCTION(model_supports);
    FUNCTION(options_create); FUNCTION(options_free); FUNCTION(options_set);
    FUNCTION(session_create); FUNCTION(session_free);
    FUNCTION(stream_start); FUNCTION(stream_push);
    FUNCTION(event_voice_activity_count); FUNCTION(event_voice_activity); FUNCTION(event_free);
#undef FUNCTION

    struct Stream {
        audiocpp_session *session = nullptr;
        QByteArray pending;
        QByteArray preRoll;
        QByteArray held;
        qint64 nextSample = 0;
        bool started = false;
        bool speaking = false;
    };

    audiocpp_registry *registry = nullptr;
    audiocpp_model *model = nullptr;
    std::array<Stream, 2> streams;

    ~Impl()
    {
        for (auto &stream : streams) if (stream.session) session_free(stream.session);
        if (model) model_free(model);
        if (registry) registry_free(registry);
        library.unload();
    }

    bool nativeFail(QString *error, const QString &message) const
    {
        const char *detail = last_error();
        return fail(error, detail && *detail ? message + QStringLiteral(": ") + QString::fromUtf8(detail) : message);
    }

    template <typename T>
    bool resolve(T &function, const char *name, QString *error)
    {
        function = reinterpret_cast<T>(library.resolve(name));
        return function || fail(error, QStringLiteral("引擎缺少语音检测接口: ") + QString::fromLatin1(name));
    }

    Stream *source(int source)
    {
        if (source < 1 || source > int(streams.size())) return nullptr;
        auto &stream = streams[size_t(source - 1)];
        return stream.session ? &stream : nullptr;
    }

    bool start(Stream &stream, QString *error) const
    {
        auto *session = stream.session;
        stream = {};
        stream.session = session;
        if (stream_start(session, nullptr) != AUDIOCPP_OK)
            return nativeFail(error, QStringLiteral("启动语音检测失败"));
        stream.started = true;
        return true;
    }

    bool processFrame(Stream &stream, QByteArrayView frame, QByteArray &emitted,
                      QList<Batch> &batches, QString *error) const
    {
        std::array<float, FrameSamples> samples;
        const auto *bytes = reinterpret_cast<const uchar *>(frame.data());
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = float(qFromLittleEndian<qint16>(bytes + i * 2)) / 32768.0f;

        audiocpp_event *raw = nullptr;
        const auto status = stream_push(stream.session, samples.data(), samples.size(), SampleRate, 1,
                                        stream.nextSample, &raw);
        const std::unique_ptr<audiocpp_event, decltype(event_free)> event(raw, event_free);
        if (status != AUDIOCPP_OK) return nativeFail(error, QStringLiteral("语音检测失败"));
        stream.nextSample += FrameSamples;

        // Silero emits at most one speech transition per frame.
        qint64 endpointSample = -1;
        const auto count = event ? event_voice_activity_count(event.get()) : 0;
        for (size_t i = 0; i < count; ++i) {
            audiocpp_voice_activity_kind kind;
            int64_t sample = -1;
            if (event_voice_activity(event.get(), i, &kind, &sample, nullptr) != AUDIOCPP_OK)
                return nativeFail(error, QStringLiteral("读取语音检测结果失败"));
            if (kind == AUDIOCPP_VOICE_ACTIVITY_SPEECH_START && !stream.speaking) {
                stream.speaking = true;
                stream.held = std::exchange(stream.preRoll, {});
            } else if (kind == AUDIOCPP_VOICE_ACTIVITY_SPEECH_END && stream.speaking) {
                endpointSample = sample;
            }
        }

        if (stream.speaking) {
            stream.held.append(frame);
            if (endpointSample >= 0) {
                const qint64 heldSamples = stream.held.size() / 2;
                const auto heldStartSample = stream.nextSample - heldSamples;
                // With hysteresis the end can be older than the hold buffer.
                // Already submitted samples stay with this utterance.
                const auto sendSamples = std::clamp(endpointSample + TailSamples - heldStartSample, qint64(0), heldSamples);
                emitted.append(QByteArrayView(stream.held).first(sendSamples * 2));
                stream.preRoll = stream.held.sliced(sendSamples * 2).right(PreRollBytes);
                stream.held.clear();
                batches.append({std::exchange(emitted, {}), true, endpointSample});
                stream.speaking = false;
            } else if (stream.held.size() > HoldBytes) {
                const auto sendBytes = stream.held.size() - HoldBytes;
                emitted.append(QByteArrayView(stream.held).first(sendBytes));
                stream.held.remove(0, sendBytes);
            }
        } else {
            stream.preRoll.append(frame);
            if (stream.preRoll.size() > PreRollBytes)
                stream.preRoll.remove(0, stream.preRoll.size() - PreRollBytes);
        }
        return true;
    }
};

OfflineVoiceActivity::OfflineVoiceActivity() = default;
OfflineVoiceActivity::~OfflineVoiceActivity() = default;

bool OfflineVoiceActivity::load(const QString &runtimePath, const QString &modelPath, QString *error)
{
    if (error) error->clear();
    if (!QFileInfo(runtimePath).isAbsolute() || !QFileInfo(runtimePath).isFile())
        return fail(error, QStringLiteral("内置转写引擎文件不存在"));
    if (!QFileInfo(modelPath).isAbsolute() || !QFileInfo(modelPath).isFile())
        return fail(error, QStringLiteral("内置语音检测模型文件不存在"));

    auto next = std::make_unique<Impl>();
    next->library.setFileName(runtimePath);
    if (!next->library.load())
        return fail(error, QStringLiteral("无法加载语音检测引擎: ") + next->library.errorString());
#define RESOLVE(name) if (!next->resolve(next->name, "audiocpp_" #name, error)) return false
    RESOLVE(last_error);
    RESOLVE(registry_create); RESOLVE(registry_free);
    RESOLVE(model_load); RESOLVE(model_free); RESOLVE(model_supports);
    RESOLVE(options_create); RESOLVE(options_free); RESOLVE(options_set);
    RESOLVE(session_create); RESOLVE(session_free);
    RESOLVE(stream_start); RESOLVE(stream_push);
    RESOLVE(event_voice_activity_count); RESOLVE(event_voice_activity); RESOLVE(event_free);
#undef RESOLVE

    if (next->registry_create(nullptr, &next->registry) != AUDIOCPP_OK || !next->registry)
        return next->nativeFail(error, QStringLiteral("初始化语音检测引擎失败"));
    const auto file = modelPath.toUtf8();
    const audiocpp_model_config selection{"silero_vad", nullptr, nullptr, nullptr};
    if (next->model_load(next->registry, file.constData(), &selection, nullptr, &next->model) != AUDIOCPP_OK || !next->model)
        return next->nativeFail(error, QStringLiteral("加载语音检测模型失败"));
    if (!next->model_supports(next->model, "vad", "streaming"))
        return fail(error, QStringLiteral("语音检测模型不支持流式处理"));

    const std::unique_ptr<audiocpp_options, decltype(next->options_free)> options(next->options_create(), next->options_free);
    const auto endpointDelay = QByteArray::number(EndpointDelayMs);
    if (!options || next->options_set(options.get(), "min_silence_duration_ms", endpointDelay.constData()) != AUDIOCPP_OK)
        return next->nativeFail(error, QStringLiteral("设置语音检测端点失败"));
    const audiocpp_backend_config backend{"cpu", 0, 1};
    for (auto &stream : next->streams) {
        if (next->session_create(next->model, "vad", "streaming", &backend, options.get(), &stream.session) != AUDIOCPP_OK ||
            !stream.session)
            return next->nativeFail(error, QStringLiteral("创建语音检测流失败"));
        if (!next->start(stream, error)) return false;
    }
    d = std::move(next);
    return true;
}

bool OfflineVoiceActivity::start(int source, QString *error)
{
    if (error) error->clear();
    auto *stream = d ? d->source(source) : nullptr;
    if (!stream) return fail(error, QStringLiteral("语音检测流不可用"));
    return d->start(*stream, error);
}

bool OfflineVoiceActivity::push(int source, QByteArrayView pcm, QList<Batch> *batches, QString *error)
{
    if (error) error->clear();
    if (!batches) return fail(error, QStringLiteral("缺少语音检测输出缓冲区"));
    batches->clear();
    auto *stream = d ? d->source(source) : nullptr;
    if (!stream || !stream->started) return fail(error, QStringLiteral("语音检测流不可用"));

    stream->pending.append(pcm);
    qsizetype consumed = 0;
    QByteArray emitted;
    while (stream->pending.size() - consumed >= FrameBytes) {
        const auto frame = QByteArrayView(stream->pending).sliced(consumed, FrameBytes);
        if (!d->processFrame(*stream, frame, emitted, *batches, error)) {
            stream->started = false;
            batches->clear();
            return false;
        }
        consumed += FrameBytes;
    }
    stream->pending.remove(0, consumed);
    if (!emitted.isEmpty()) batches->append({std::move(emitted), false, -1});
    return true;
}

QByteArray OfflineVoiceActivity::takeTail(int source)
{
    auto *stream = d ? d->source(source) : nullptr;
    if (!stream || !stream->started) return {};
    stream->started = false;
    stream->preRoll.clear();
    auto pending = std::exchange(stream->pending, {});
    if (!stream->speaking) return {};
    stream->speaking = false;
    if (pending.size() % 2 != 0) pending.chop(1);
    auto tail = std::exchange(stream->held, {});
    tail += pending;
    return tail;
}
