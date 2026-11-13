#include "offline_transcription_engine.h"
#include "offline_transcription_text.h"
#include "vendor/audio.cpp/audiocpp.h"

#include <QByteArrayView>
#include <QFileInfo>
#include <QLibrary>
#include <QtEndian>
#include <array>
#include <vector>

namespace {
constexpr qsizetype ContextBytes = 4 * OfflineTranscriptionEngine::WindowBytes;

void appendTranscript(QString &text, QStringView continuation)
{
    if (!text.isEmpty() && !continuation.isEmpty() && text.back().isLetterOrNumber() &&
        continuation.front().isLetterOrNumber() &&
        !OfflineTranscriptionDetail::usesCharacterUnits(text.back().unicode()) &&
        !OfflineTranscriptionDetail::usesCharacterUnits(continuation.front().unicode()))
        text += QLatin1Char(' ');
    text += continuation;
}

QStringView withoutLeadingMarks(QStringView text)
{
    // Keep opening quotes/brackets belonging to the next sentence.
    while (!text.isEmpty() && (text.front().isSpace() || QStringView(u"。！？.!?，,：:；;”’」』）》】〕〉）)]}").contains(text.front())))
        text = text.sliced(1);
    return text;
}

bool hasWords(QStringView text)
{
    return std::any_of(text.begin(), text.end(), [](QChar ch) { return ch.isLetterOrNumber() || ch.isHighSurrogate(); });
}

bool sameTerminalMark(QStringView first, QStringView second)
{
    for (const auto marks : {QStringView(u"。."), QStringView(u"？?"), QStringView(u"！!")}) {
        const auto contains = [marks](QStringView text) {
            return std::any_of(text.begin(), text.end(), [marks](QChar ch) { return marks.contains(ch); });
        };
        if (contains(first) && contains(second)) return true;
    }
    return false;
}

struct Draft {
    QString prefix;
    QString snapshot;
    qsizetype start = 0;

    QString text() const
    {
        auto result = prefix;
        appendTranscript(result, QStringView(snapshot).sliced(start));
        return result.trimmed();
    }

    bool revise(QString current, double shift, bool final, OfflineTranscriptionEngine::Result &result)
    {
        const auto alignment = alignOfflineTranscript(snapshot, current, shift);
        const auto oldEnds = offlineSentenceEnds(snapshot);
        qsizetype currentStart = -1;
        if (shift > 0 && !snapshot.isEmpty()) {
            const OfflineTranscriptMatch *anchor = nullptr;
            for (qsizetype i = 1; i < alignment.matches.size(); ++i) {
                const auto &first = alignment.matches[i - 1];
                const auto &next = alignment.matches[i];
                if (first.previousIndex + 1 == next.previousIndex && first.currentIndex + 1 == next.currentIndex) {
                    anchor = &first;
                    break;
                }
            }
            if (!anchor && alignment.matches.size() == 1 &&
                (alignment.previousUnits <= 2 || alignment.currentUnits <= 2))
                anchor = &alignment.matches.front();
            if (!anchor) return false;
            if (anchor->previousStart >= start) {
                // A matched continuation can outlive the punctuation it
                // follows. Publish an interior boundary just before the
                // overlap before it disappears with the old audio.
                auto retainedStart = start;
                for (const auto end : oldEnds) {
                    if (end <= retainedStart || end > anchor->previousStart) continue;
                    if (hasWords(QStringView(snapshot).sliced(end, anchor->previousStart - end))) continue;
                    auto sentence = std::move(prefix);
                    prefix.clear();
                    appendTranscript(sentence, QStringView(snapshot).sliced(retainedStart, end - retainedStart));
                    if (hasWords(sentence)) result.sentences.append(sentence.trimmed());
                    retainedStart = end;
                }
                appendTranscript(prefix, QStringView(snapshot).sliced(retainedStart, anchor->previousStart - retainedStart));
                currentStart = anchor->currentStart;
            }
        }
        if (start > 0 && currentStart < 0) {
            // Clip words that already belong to a committed capsule even if
            // the next decode changes their punctuation.
            const OfflineTranscriptMatch *before = nullptr;
            for (const auto &match : alignment.matches) {
                if (match.previousEnd > start) break;
                before = &match;
            }
            if (!before || hasWords(QStringView(snapshot).sliced(before->previousEnd, start - before->previousEnd)))
                return false;
            const auto remainder = QStringView(current).sliced(before->currentEnd);
            currentStart = current.size() - withoutLeadingMarks(remainder).size();
        }
        if (currentStart < 0) currentStart = 0;

        const auto ends = offlineSentenceEnds(current);
        for (const auto end : ends) {
            if (end <= currentStart) continue;
            const auto after = QStringView(current).sliced(end);
            if (!hasWords(after)) break; // The last sentence stays in the draft, including at drain.
            if (!final) {
                const OfflineTranscriptMatch *before = nullptr;
                for (const auto &match : alignment.matches) {
                    if (match.currentEnd > end) break;
                    before = &match;
                }
                if (!before || before->previousEnd <= start || before->currentEnd <= currentStart) break;
                const auto oldEnd = std::upper_bound(oldEnds.begin(), oldEnds.end(), before->previousEnd);
                const auto currentMarks = QStringView(current).sliced(before->currentEnd, end - before->currentEnd);
                if (oldEnd == oldEnds.end() || hasWords(currentMarks)) break;
                const auto oldMarks = QStringView(snapshot).sliced(before->previousEnd, *oldEnd - before->previousEnd);
                if (hasWords(oldMarks) || !sameTerminalMark(oldMarks, currentMarks)) break;
            }
            auto sentence = std::move(prefix);
            prefix.clear();
            appendTranscript(sentence, QStringView(current).sliced(currentStart, end - currentStart));
            if (hasWords(sentence)) result.sentences.append(sentence.trimmed());
            currentStart = end;
        }
        snapshot = std::move(current);
        start = currentStart;
        return true;
    }

    void appendFallback(QString current, bool final, OfflineTranscriptionEngine::Result &result)
    {
        auto preceding = text();
        const auto ends = offlineSentenceEnds(preceding);
        if (!ends.isEmpty() && ends.back() == preceding.size()) {
            // The previous decode stopped here. Its edge punctuation is
            // unreliable when we can only decode the new audio separately.
            constexpr QStringView marks = u"。！？.!?";
            auto cut = preceding.size();
            while (cut > 0 && !preceding[cut - 1].isLetterOrNumber()) --cut;
            auto suffix = preceding.mid(cut);
            suffix.removeIf([marks](QChar ch) { return marks.contains(ch); });
            preceding.truncate(cut);
            preceding += suffix;
        }
        if (final) {
            qsizetype from = 0;
            for (const auto end : offlineSentenceEnds(preceding)) {
                const auto sentence = QStringView(preceding).sliced(from, end - from);
                if (hasWords(sentence)) result.sentences.append(sentence.trimmed().toString());
                from = end;
            }
            preceding.remove(0, from);
        }
        prefix = std::move(preceding);
        snapshot = std::move(current);
        start = 0;
    }
};
}

struct OfflineTranscriptionEngine::Impl {
    QLibrary library;
#define FUNCTION(name) decltype(&audiocpp_##name) name = nullptr
    FUNCTION(abi_version); FUNCTION(last_error); FUNCTION(registry_create); FUNCTION(registry_free);
    FUNCTION(model_load); FUNCTION(model_free); FUNCTION(model_family); FUNCTION(model_supports);
    FUNCTION(session_create); FUNCTION(session_free); FUNCTION(request_create); FUNCTION(request_free); FUNCTION(request_set_audio);
    FUNCTION(request_set_option); FUNCTION(stream_start); FUNCTION(stream_push);
    FUNCTION(stream_finish); FUNCTION(stream_reset);
    FUNCTION(result_text); FUNCTION(result_free);
#undef FUNCTION
    audiocpp_registry *registry = nullptr;
    audiocpp_model *model = nullptr;
    audiocpp_session *session = nullptr;
    struct Stream {
        QByteArray pending;
        QByteArray context;
        Draft draft;
        bool active = false;
        qsizetype decodedContextBytes = 0;
    };
    std::array<Stream, 2> streams;
    bool ready = false;

    ~Impl() { clear(); }
    void clear() {
        ready = false;
        streams = {};
        if (session) session_free(session);
        session = nullptr;
        if (model) model_free(model);
        model = nullptr;
        if (registry && registry_free) registry_free(registry);
        registry = nullptr;
        library.unload();
    }
    bool fail(QString *error, const QString &message) {
        if (error) *error = message;
        return false;
    }
    bool nativeFail(QString *error, const QString &context) {
        const char *detail = last_error ? last_error() : nullptr;
        return fail(error, context + (detail && *detail ? QStringLiteral(": ") + QString::fromUtf8(detail) : QString()));
    }
    template<typename T> bool symbol(T &fn, const char *name, QString *error) {
        fn = reinterpret_cast<T>(library.resolve(name));
        return fn ? true : fail(error, QStringLiteral("引擎缺少接口: ") + QString::fromLatin1(name));
    }
    Stream *source(int source, QString *error) {
        if (!ready) { fail(error, QStringLiteral("转写引擎尚未加载")); return nullptr; }
        if (source != 1 && source != 2) { fail(error, QStringLiteral("无效的音频来源")); return nullptr; }
        return &streams[size_t(source - 1)];
    }
    bool windowFailure(QString *error, const QString &context) {
        nativeFail(error, context);
        stream_reset(session);
        return false;
    }
    bool recognizeWindow(QByteArrayView pcm, QString &output, QString *error) {
        const std::unique_ptr<audiocpp_request, decltype(request_free)> request(request_create(), request_free);
        if (!request)
            return nativeFail(error, QStringLiteral("创建转写请求失败"));
        // Qwen requires the input format even when starting with zero frames.
        if (request_set_audio(request.get(), nullptr, 0, 16000, 1) != AUDIOCPP_OK)
            return nativeFail(error, QStringLiteral("设置音频格式失败"));
        if (request_set_option(request.get(), "audio_chunk_seconds", "4.0") != AUDIOCPP_OK)
            return nativeFail(error, QStringLiteral("设置流式窗口失败"));
        if (request_set_option(request.get(), "max_tokens", "64") != AUDIOCPP_OK)
            return nativeFail(error, QStringLiteral("设置转写输出上限失败"));

        std::vector<float> samples(size_t(pcm.size() / 2));
        const auto *bytes = reinterpret_cast<const uchar *>(pcm.data());
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = float(qFromLittleEndian<qint16>(bytes + i * 2)) / 32768.0f;

        // Qwen already decodes each window independently. A complete native
        // stream per window keeps the shared session free of source history.
        if (stream_start(session, request.get()) != AUDIOCPP_OK)
            return windowFailure(error, QStringLiteral("启动转写流失败"));
        if (stream_push(session, samples.data(), samples.size(), 16000, 1, 0, nullptr) != AUDIOCPP_OK)
            return windowFailure(error, QStringLiteral("转写失败"));
        audiocpp_result *raw = nullptr;
        const auto status = stream_finish(session, &raw);
        const std::unique_ptr<audiocpp_result, decltype(result_free)> result(raw, result_free);
        if (status != AUDIOCPP_OK || !result)
            return windowFailure(error, QStringLiteral("结束转写失败"));
        const char *text = nullptr;
        const auto textStatus = result_text(result.get(), &text, nullptr);
        if (textStatus == AUDIOCPP_ERR_NOT_AVAILABLE) return true;
        if (textStatus != AUDIOCPP_OK)
            return nativeFail(error, QStringLiteral("读取转写结果失败"));
        if (text && *text) output = QString::fromUtf8(text).trimmed();
        return true;
    }

    bool decode(Stream &source, QByteArrayView input, bool final, Result &result, QString *error)
    {
        const qsizetype previousBytes = source.decodedContextBytes;
        source.context += input;
        qsizetype shiftedBytes = 0;
        if (source.context.size() > ContextBytes) {
            // Keep whole-stride origins for previews and the partial drain.
            source.context.remove(0, WindowBytes);
            shiftedBytes = WindowBytes;
        }
        QString text;
        if (!recognizeWindow(source.context, text, error)) return false;
        const double shift = previousBytes ? double(shiftedBytes) / double(previousBytes) : 0;
        if ((text.isEmpty() && !source.draft.snapshot.isEmpty()) || !source.draft.revise(text, shift, final, result)) {
            // There is no safe seam. Recover the new PCM exactly once rather
            // than duplicating or discarding the uncertain overlapping text.
            text.clear();
            if (!recognizeWindow(input, text, error)) return false;
            if (!text.isEmpty()) source.draft.appendFallback(std::move(text), final, result);
            else {
                source.draft.prefix = source.draft.text();
                source.draft.snapshot.clear();
                source.draft.start = 0;
            }
            source.context = input.toByteArray();
        }
        source.decodedContextBytes = source.context.size();
        result.draft = source.draft.text();
        result.updated = true;
        return true;
    }
};

OfflineTranscriptionEngine::OfflineTranscriptionEngine() : d(std::make_unique<Impl>()) {}
OfflineTranscriptionEngine::~OfflineTranscriptionEngine() = default;

bool OfflineTranscriptionEngine::load(const QString &runtimePath, const QString &modelPath, QString *error) {
    if (error) error->clear();
    if (!QFileInfo(runtimePath).isAbsolute() || !QFileInfo(runtimePath).isFile())
        return d->fail(error, QStringLiteral("内置转写引擎文件不存在"));
    if (!QFileInfo(modelPath).isAbsolute() || !QFileInfo(modelPath).isFile())
        return d->fail(error, QStringLiteral("请选择有效的模型文件"));
    auto next = std::make_unique<Impl>();
    next->library.setFileName(runtimePath);
    if (!next->library.load()) return next->fail(error, QStringLiteral("无法加载引擎: ") + next->library.errorString());
#define RESOLVE(name) if (!next->symbol(next->name, "audiocpp_" #name, error)) return false
    RESOLVE(abi_version); RESOLVE(last_error); RESOLVE(registry_create); RESOLVE(registry_free);
    RESOLVE(model_load); RESOLVE(model_free); RESOLVE(model_family); RESOLVE(model_supports);
    RESOLVE(session_create); RESOLVE(session_free); RESOLVE(request_create); RESOLVE(request_free); RESOLVE(request_set_audio);
    RESOLVE(request_set_option); RESOLVE(stream_start); RESOLVE(stream_push);
    RESOLVE(stream_finish); RESOLVE(stream_reset);
    RESOLVE(result_text); RESOLVE(result_free);
#undef RESOLVE
    const auto version = next->abi_version();
    if ((version >> 16) != AUDIOCPP_ABI_VERSION_MAJOR || ((version >> 8) & 255) < AUDIOCPP_ABI_VERSION_MINOR)
        return next->fail(error, QStringLiteral("audio.cpp 运行库 ABI 不兼容"));
    if (next->registry_create(nullptr, &next->registry) != AUDIOCPP_OK || !next->registry)
        return next->nativeFail(error, QStringLiteral("初始化引擎失败"));
    const QByteArray modelFile = QFileInfo(modelPath).absoluteFilePath().toUtf8();
    const audiocpp_model_config selection{"qwen3_asr", nullptr, nullptr, nullptr};
#if defined(Q_OS_MACOS)
    const audiocpp_backend_config backend{"metal", 0, 4};
#else
    const audiocpp_backend_config backend{"vulkan", 0, 4};
#endif
    if (next->model_load(next->registry, modelFile.constData(), &selection, nullptr, &next->model) != AUDIOCPP_OK || !next->model)
        return next->nativeFail(error, QStringLiteral("加载转写模型失败"));
    const char *family = next->model_family(next->model);
    if (!family || QByteArray(family) != "qwen3_asr" || !next->model_supports(next->model, "asr", "streaming"))
        return next->fail(error, QStringLiteral("模型不支持 Qwen3-ASR 流式转写"));
    if (next->session_create(next->model, "asr", "streaming", &backend, nullptr, &next->session) != AUDIOCPP_OK || !next->session)
        return next->nativeFail(error, QStringLiteral("GPU 引擎初始化失败"));
    next->ready = true;
    d = std::move(next);
    return true;
}
bool OfflineTranscriptionEngine::start(int source, QString *error) {
    if (error) error->clear();
    auto *s = d->source(source, error);
    if (!s) return false;
    *s = {};
    s->active = true;
    return true;
}
bool OfflineTranscriptionEngine::push(int source, const QByteArray &pcm16, Result *result, QString *error) {
    if (result) *result = {};
    if (error) error->clear();
    auto *s = d->source(source, error); if (!s) return false;
    if (!s->active) return d->fail(error, QStringLiteral("转写流尚未启动"));
    if (pcm16.size() % 2) return d->fail(error, QStringLiteral("音频数据不是完整的 PCM16"));
    if (pcm16.isEmpty()) return true;
    s->pending.append(pcm16);
    Result output;
    qsizetype consumed = 0;
    while (s->pending.size() - consumed >= WindowBytes) {
        if (!d->decode(*s, QByteArrayView(s->pending).sliced(consumed, WindowBytes), false, output, error)) {
            *s = {};
            return false;
        }
        consumed += WindowBytes;
    }
    s->pending.remove(0, consumed);
    if (result) *result = std::move(output);
    return true;
}
bool OfflineTranscriptionEngine::finish(int source, Result *result, QString *error) {
    if (result) *result = {};
    if (error) error->clear();
    auto *s = d->source(source, error); if (!s) return false;
    if (!s->active) return true;
    Result output;
    bool ok = true;
    if (!s->pending.isEmpty()) ok = d->decode(*s, s->pending, true, output, error);
    else if (!s->draft.snapshot.isEmpty() || !s->draft.prefix.isEmpty()) {
        const auto snapshot = s->draft.snapshot;
        s->draft.revise(snapshot, 0, true, output);
        output.draft = s->draft.text();
        output.updated = true;
    }
    *s = {};
    if (!ok) return false;
    if (result) *result = std::move(output);
    return true;
}
void OfflineTranscriptionEngine::reset(int source) {
    if (!d->ready || (source != 1 && source != 2)) return;
    d->streams[size_t(source - 1)] = {};
}
void OfflineTranscriptionEngine::unload() { d->clear(); }
bool OfflineTranscriptionEngine::isLoaded() const { return d->ready; }
