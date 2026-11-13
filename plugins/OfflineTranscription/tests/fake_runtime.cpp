#include "audiocpp.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <cstring>
#include <cstdlib>
#include <array>
#include <atomic>
#include <vector>
struct audiocpp_registry {};
struct audiocpp_model { bool vad = false; mutable bool createdSession = false; };
struct audiocpp_options { int minSilenceMs = 800; };
struct audiocpp_request { std::string window; bool audioContract = false; int maxTokens = 512; };
struct audiocpp_session {
    static constexpr size_t FramesPerSecond = 16000;
    static constexpr size_t VadFrames = 512;
    size_t counterIndex = 0;
    std::string text;
    size_t pending = 0;
    size_t received = 0;
    size_t windowFrames = FramesPerSecond;
    bool positive = false;
    bool negative = false;
    bool active = false;
    int amplitude = 0;
    bool vad = false;
    bool speech = false;
    size_t quietFrames = 0;
    size_t minSilenceFrames = 0;
    bool firstPhrase = false;
    bool secondPhrase = false;

    void appendPcmGroup(bool tail) {
        std::string word = positive ? (negative ? "mixed" : "mic") : (negative ? "spk" : "");
        if (std::getenv("OFFLINE_FAKE_VAD_SCENARIO") && positive && !negative)
            word = firstPhrase ? (secondPhrase ? "first second" : "first") : (secondPhrase ? "second" : "");
        if (std::getenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION")) {
            // Encode known words by sample amplitude so text follows the
            // actual PCM retained in each successive overlapping window.
            switch (amplitude) {
            case 1000: word = "We arrived"; break;
            case 1100: word = "early."; break;
            case 1200: word = "The room"; break;
            case 1300: word = "was quiet."; break;
            case 1400: word = "Then"; break;
            case 1500: word = "we left"; break;
            case 1600: word = "together."; break;
            case 1700: word = "First line"; break;
            case 1800: word = "Here comes"; break;
            case 1900: word = "another phrase"; break;
            case 2000: word = "and it ends"; break;
            case 2100: word = "right here."; break;
            default: break;
            }
        }
        if (!word.empty()) {
            if (tail && !std::getenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION")) word += " tail";
            if (!text.empty()) word.insert(0, " ");
            text += word;
        }
        pending = 0;
        amplitude = 0;
        positive = negative = false;
        firstPhrase = secondPhrase = false;
    }

    void reset() {
        text.clear();
        pending = received = 0;
        amplitude = 0;
        positive = negative = active = false;
        speech = firstPhrase = secondPhrase = false;
        quietFrames = 0;
    }
};
struct audiocpp_result { std::string text; };
struct VadActivity { audiocpp_voice_activity_kind kind; int64_t sample; float probability; };
struct audiocpp_event { audiocpp_result result; std::vector<VadActivity> activity; };
static std::atomic<size_t> nextSessionIndex{0};
static std::atomic<int> modelLoads{0}, sessionCreates{0};
// Source-specific counters are indexed by native session creation order.
// With a shared session, the aggregate (source=0) covers all native calls.
static std::array<std::atomic<int>, 2> pushCounts{}, startCounts{}, finishCounts{}, tokenLimits{};
static std::array<std::atomic<uint64_t>, 2> frameCounts{};
template<typename T> T counterValue(const std::array<std::atomic<T>, 2> &values, int source) {
    if (source == 1 || source == 2) return values[size_t(source - 1)].load();
    return values[0].load() + values[1].load();
}
extern "C" {
AUDIOCPP_API void offline_fake_reset_counters() {
    modelLoads = sessionCreates = 0;
    for (size_t i = 0; i < 2; ++i) {
        pushCounts[i] = 0; startCounts[i] = 0; finishCounts[i] = 0;
        frameCounts[i] = 0; tokenLimits[i] = 0;
    }
}
AUDIOCPP_API int offline_fake_model_load_count() { return modelLoads.load(); }
AUDIOCPP_API int offline_fake_session_create_count() { return sessionCreates.load(); }
AUDIOCPP_API int offline_fake_push_count(int source) { return counterValue(pushCounts, source); }
AUDIOCPP_API int offline_fake_start_count(int source) { return counterValue(startCounts, source); }
AUDIOCPP_API int offline_fake_finish_count(int source) { return counterValue(finishCounts, source); }
AUDIOCPP_API uint64_t offline_fake_frame_count(int source) { return counterValue(frameCounts, source); }
AUDIOCPP_API int offline_fake_max_tokens(int source) { return counterValue(tokenLimits, source); }
uint32_t audiocpp_abi_version() { return std::getenv("OFFLINE_FAKE_BAD_ABI") ? (2 << 16) : (3 << 8); }
const char *audiocpp_last_error() { return "fake failure"; }
audiocpp_options *audiocpp_options_create() { return new audiocpp_options; }
void audiocpp_options_free(audiocpp_options *options) { delete options; }
audiocpp_status audiocpp_options_set(audiocpp_options *options, const char *key, const char *value) {
    if (!options || !key || !value || std::strcmp(key, "min_silence_duration_ms"))
        return AUDIOCPP_ERR_INVALID_ARGUMENT;
    options->minSilenceMs = std::atoi(value);
    return options->minSilenceMs > 0 ? AUDIOCPP_OK : AUDIOCPP_ERR_INVALID_ARGUMENT;
}
audiocpp_status audiocpp_registry_create(const char *, audiocpp_registry **p) { *p = new audiocpp_registry; return AUDIOCPP_OK; }
void audiocpp_registry_free(audiocpp_registry *p) { delete p; }
audiocpp_status audiocpp_model_load(audiocpp_registry *, const char *, const audiocpp_model_config *config, const audiocpp_options *, audiocpp_model **p) {
    const bool vad = config && config->family_hint && !std::strcmp(config->family_hint, "silero_vad");
    if (!vad) ++modelLoads;
    *p = new audiocpp_model{vad};
    return AUDIOCPP_OK;
}
void audiocpp_model_free(audiocpp_model *p) { delete p; }
const char *audiocpp_model_family(const audiocpp_model *model) { return model->vad ? "silero_vad" : "qwen3_asr"; }
int audiocpp_model_supports(const audiocpp_model *model, const char *task, const char *mode) { return !std::strcmp(task, model->vad ? "vad" : "asr") && !std::strcmp(mode, "streaming"); }
audiocpp_status audiocpp_session_create(const audiocpp_model *model, const char *task, const char *mode, const audiocpp_backend_config *config, const audiocpp_options *options, audiocpp_session **p) {
    if (!audiocpp_model_supports(model, task, mode) || !config || !config->backend) return AUDIOCPP_ERR_INVALID_ARGUMENT;
    if (model->vad) {
        if (std::strcmp(config->backend, "cpu") || !options || options->minSilenceMs != 800)
            return AUDIOCPP_ERR_INVALID_ARGUMENT;
        *p = new audiocpp_session;
        (*p)->vad = true;
        (*p)->minSilenceFrames = options->minSilenceMs * audiocpp_session::FramesPerSecond / 1000;
        return AUDIOCPP_OK;
    }
    if (model->createdSession) return AUDIOCPP_ERR_INVALID_ARGUMENT;
    if (std::getenv("OFFLINE_FAKE_GPU_FAILURE")) return AUDIOCPP_ERR_RUNTIME;
#if defined(__APPLE__)
    if (std::strcmp(config->backend, "metal")) return AUDIOCPP_ERR_RUNTIME;
#else
    if (std::strcmp(config->backend, "vulkan")) return AUDIOCPP_ERR_RUNTIME;
#endif
    model->createdSession = true;
    ++sessionCreates;
    *p = new audiocpp_session{nextSessionIndex++ % 2, {}};
    return AUDIOCPP_OK;
}
void audiocpp_session_free(audiocpp_session *p) { delete p; }
audiocpp_status audiocpp_stream_policy(const audiocpp_session *p, audiocpp_stream_input_kind *input,
                                        audiocpp_stream_output_kind *output, int64_t *chunkSamples, double *chunkSeconds) {
    const auto frames = p->vad ? audiocpp_session::VadFrames : audiocpp_session::FramesPerSecond;
    if (input) *input = AUDIOCPP_STREAM_INPUT_AUDIO_CHUNKS;
    if (output) *output = AUDIOCPP_STREAM_OUTPUT_PULL_EVENTS;
    if (chunkSamples) *chunkSamples = frames;
    if (chunkSeconds) *chunkSeconds = double(frames) / audiocpp_session::FramesPerSecond;
    return AUDIOCPP_OK;
}
audiocpp_request *audiocpp_request_create() { return new audiocpp_request; }
void audiocpp_request_free(audiocpp_request *p) { delete p; }
audiocpp_status audiocpp_request_set_option(audiocpp_request *p, const char *key, const char *value) {
    if (!std::strcmp(key, "audio_chunk_seconds")) p->window = value;
    else if (!std::strcmp(key, "max_tokens")) {
        if (std::getenv("OFFLINE_FAKE_TOKEN_OPTION_FAILURE")) return AUDIOCPP_ERR_INVALID_ARGUMENT;
        p->maxTokens = std::atoi(value);
    } else return AUDIOCPP_ERR_INVALID_ARGUMENT;
    return AUDIOCPP_OK;
}
audiocpp_status audiocpp_request_set_audio(audiocpp_request *p, const float *, size_t frames, int rate, int channels) { if (frames != 0 || rate != 16000 || channels != 1) return AUDIOCPP_ERR_INVALID_ARGUMENT; p->audioContract = true; return AUDIOCPP_OK; }
audiocpp_status audiocpp_stream_start(audiocpp_session *p, const audiocpp_request *request) {
    if (p->vad) {
        if (request) return AUDIOCPP_ERR_INVALID_ARGUMENT;
        p->reset();
        p->active = true;
        return AUDIOCPP_OK;
    }
    ++startCounts[p->counterIndex];
    if (!request || (request->window != "1.0" && request->window != "4.0") ||
        !request->audioContract || request->maxTokens != 64)
        return AUDIOCPP_ERR_INVALID_ARGUMENT;
    tokenLimits[p->counterIndex] = request->maxTokens;
    p->reset();
    p->windowFrames = (request->window == "4.0" ? 4 : 1) * audiocpp_session::FramesPerSecond;
    p->active = true;
    return AUDIOCPP_OK;
}
audiocpp_status audiocpp_stream_push(audiocpp_session *p, const float *samples, size_t frames, int rate, int channels, int64_t start, audiocpp_event **event) {
    if (p->vad) {
        if (!p->active || rate != 16000 || channels != 1 || start < 0 || uint64_t(start) != p->received ||
            frames != audiocpp_session::VadFrames || !samples || !event)
            return AUDIOCPP_ERR_INVALID_ARGUMENT;
        const bool voiced = std::any_of(samples, samples + frames, [](float sample) {
            return std::abs(sample) >= 1000.f / 32768;
        });
        *event = nullptr;
        p->received += frames;
        if (voiced) {
            p->quietFrames = 0;
            if (!p->speech) *event = new audiocpp_event{{}, {{AUDIOCPP_VOICE_ACTIVITY_SPEECH_START, start, 1.f}}};
            p->speech = true;
        } else if (p->speech) {
            p->quietFrames += frames;
            if (p->quietFrames >= p->minSilenceFrames) {
                *event = new audiocpp_event{{}, {{AUDIOCPP_VOICE_ACTIVITY_SPEECH_END,
                                                 int64_t(p->received - p->quietFrames), 0.f}}};
                p->speech = false;
                p->quietFrames = 0;
            }
        }
        return AUDIOCPP_OK;
    }
    ++pushCounts[p->counterIndex];
    frameCounts[p->counterIndex] += frames;
    if (!p->active || rate != 16000 || channels != 1 || start < 0 || uint64_t(start) != p->received ||
        frames > p->windowFrames - p->received)
        return AUDIOCPP_ERR_INVALID_ARGUMENT;
    if (std::getenv("OFFLINE_FAKE_PUSH_FAILURE")) return AUDIOCPP_ERR_RUNTIME;
    p->received += frames;
    if (event) *event = nullptr;
    const bool scripted = std::getenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION");
    const bool vadScenario = std::getenv("OFFLINE_FAKE_VAD_SCENARIO");
    for (size_t i = 0; i < frames; ++i) {
        p->positive = p->positive || samples[i] > 0;
        p->negative = p->negative || samples[i] < 0;
        if (scripted && p->pending == 0) p->amplitude = int(samples[i] * 32768);
        if (vadScenario) {
            p->firstPhrase = p->firstPhrase || samples[i] == 1000.f / 32768;
            p->secondPhrase = p->secondPhrase || samples[i] == 2000.f / 32768;
        }
        if (++p->pending == audiocpp_session::FramesPerSecond) p->appendPcmGroup(false);
    }
    if (event && p->received == p->windowFrames && !p->text.empty())
        *event = new audiocpp_event{{p->text}, {}};
    return AUDIOCPP_OK;
}
audiocpp_status audiocpp_stream_next_event(audiocpp_session *, audiocpp_event **p) { *p = nullptr; return AUDIOCPP_OK; }
audiocpp_status audiocpp_stream_finish(audiocpp_session *p, audiocpp_result **out) {
    if (p->vad) return AUDIOCPP_ERR_INVALID_ARGUMENT;
    ++finishCounts[p->counterIndex];
    if (!p->active || !p->received) return AUDIOCPP_ERR_INVALID_ARGUMENT;
    if (p->pending) p->appendPcmGroup(true);
    if (std::getenv("OFFLINE_FAKE_CONTEXT_PUNCTUATION") && !p->text.empty()) {
        const std::string firstLine = "First line";
        if (p->received >= 4 * audiocpp_session::FramesPerSecond && p->text.compare(0, firstLine.size() + 1, firstLine + ' ') == 0)
            p->text.insert(firstLine.size(), 1, '.');
        if (p->text.back() != '.') p->text += '.';
    }
    *out = new audiocpp_result{p->text};
    p->active = false;
    return AUDIOCPP_OK;
}
audiocpp_status audiocpp_stream_reset(audiocpp_session *p) { p->reset(); return AUDIOCPP_OK; }
void audiocpp_event_free(audiocpp_event *p) { delete p; }
size_t audiocpp_event_voice_activity_count(const audiocpp_event *p) { return p ? p->activity.size() : 0; }
audiocpp_status audiocpp_event_voice_activity(const audiocpp_event *p, size_t index, audiocpp_voice_activity_kind *kind,
                                              int64_t *sample, float *probability) {
    if (!p || index >= p->activity.size()) return AUDIOCPP_ERR_INVALID_ARGUMENT;
    const auto &activity = p->activity[index];
    if (kind) *kind = activity.kind;
    if (sample) *sample = activity.sample;
    if (probability) *probability = activity.probability;
    return AUDIOCPP_OK;
}
const audiocpp_result *audiocpp_event_as_result(const audiocpp_event *p) { return &p->result; }
audiocpp_status audiocpp_result_text(const audiocpp_result *p, const char **out, const char **language) { if (out) *out = p->text.c_str(); if (language) *language = ""; return AUDIOCPP_OK; }
void audiocpp_result_free(audiocpp_result *p) { delete p; }
}
