#include "offline_transcription_controller.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QtTest>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <span>
#include <vector>
#if defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

using namespace AudioPipline;

namespace {
constexpr int SampleRate = 16000;
constexpr int FramesPerBlock = 3200;
constexpr int BlockMs = 200;
constexpr int SpeechCycleMs = 6000;
constexpr int SpeechStartMs = 600;
constexpr int PcmBytesPerMs = SampleRate * 2 / 1000;
constexpr double MiB = 1024.0 * 1024.0;

int setting(const char *name, int fallback, int minimum, int maximum)
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok ? std::clamp(value, minimum, maximum) : fallback;
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, size_t(std::ceil(values.size() * fraction) - 1))];
}

void appendBounded(std::vector<double> &values, double value)
{
    // Default ten-minute tests retain every frame interval. Longer custom runs
    // retain at most the latest 120,000 intervals (about 33 minutes at 60 Hz).
    if (values.size() >= 120000) values.erase(values.begin(), values.begin() + 10000);
    values.push_back(value);
}

class RenderProbe final : public QObject {
public:
    QString start(std::function<void(qint64)> onFrame)
    {
#if defined(Q_OS_MACOS)
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
#endif
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QtQuick.Window
Window {
    width: 320; height: 120
    title: "ASR performance probe"
    visible: false
    color: "#16242c"
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 18
        text: "Offline ASR · frame timing"
        color: "#f1f5f9"
        font.pixelSize: 15
    }
    Rectangle {
        y: 68; width: 24; height: 24; radius: 6
        color: "#5ee0b5"
        SequentialAnimation on x {
            running: true; loops: Animation.Infinite
            NumberAnimation { from: 12; to: 284; duration: 1000 }
            NumberAnimation { from: 284; to: 12; duration: 1000 }
        }
    }
}
)qml", QUrl(QStringLiteral("inmemory:/OfflineAsrRenderProbe.qml")));
        QElapsedTimer loading; loading.start();
        while (component.isLoading() && loading.elapsed() < 10000) QTest::qWait(10);
        if (!component.isReady()) return component.errorString().isEmpty()
            ? QStringLiteral("render probe component did not finish loading") : component.errorString();
        std::unique_ptr<QObject> object(component.create());
        auto *candidate = qobject_cast<QQuickWindow *>(object.get());
        if (!candidate) return component.errorString().isEmpty() ? QStringLiteral("render probe did not create a window") : component.errorString();
        window.reset(candidate);
        object.release();
        // Keep the probe visible when terminal/editor windows regain focus;
        // macOS otherwise suspends frames for a fully occluded test window.
        window->setFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        clock.start();
        connection = QObject::connect(window.get(), &QQuickWindow::afterFrameEnd, this,
            [this, onFrame] {
                // The rendering thread only reads the clock. All statistics
                // are mutated on the GUI thread using this captured timestamp.
                const qint64 timestamp = clock.nsecsElapsed();
                QMetaObject::invokeMethod(this, [onFrame, timestamp] { onFrame(timestamp); }, Qt::QueuedConnection);
            }, Qt::DirectConnection);
        window->show();
        return {};
    }

    QString graphicsApi() const
    {
        if (!window) return QStringLiteral("none");
        switch (window->rendererInterface()->graphicsApi()) {
        case QSGRendererInterface::Metal: return QStringLiteral("Metal");
        case QSGRendererInterface::Software: return QStringLiteral("Software");
        case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
        case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
        case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
        default: return QStringLiteral("other");
        }
    }

    ~RenderProbe() override
    {
        QObject::disconnect(connection);
        // Destroy the window (and stop its render thread callbacks) before the
        // callback's captured measurements leave their enclosing scope.
        window.reset();
    }

private:
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    QElapsedTimer clock;
    QMetaObject::Connection connection;
};

struct Resources {
    double footprintMiB = 0;
    double cpuSeconds = 0;
};

Resources resources()
{
    Resources out;
#if defined(Q_OS_MACOS)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        out.footprintMiB = double(info.phys_footprint) / MiB;
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        out.cpuSeconds = double(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
                         double(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.0;
#endif
    return out;
}

QByteArray readPcm(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray wav = file.readAll();
    if (wav.size() < 12 || wav.mid(0, 4) != "RIFF" || wav.mid(8, 4) != "WAVE") return {};
    QByteArray pcm;
    bool format = false;
    for (qsizetype pos = 12; pos + 8 <= wav.size();) {
        const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wav.constData() + pos + 4));
        const auto kind = wav.mid(pos, 4);
        pos += 8;
        if (quint64(size) > quint64(wav.size() - pos)) return {};
        if (kind == "fmt " && size >= 16) {
            const auto *p = reinterpret_cast<const uchar *>(wav.constData() + pos);
            format = qFromLittleEndian<quint16>(p) == 1 && qFromLittleEndian<quint16>(p + 2) == 1 &&
                     qFromLittleEndian<quint32>(p + 4) == SampleRate && qFromLittleEndian<quint16>(p + 14) == 16;
        } else if (kind == "data") pcm = wav.mid(pos, size);
        pos += size + (size & 1u);
    }
    return format && pcm.size() % 2 == 0 ? pcm : QByteArray{};
}

QString normalizedTranscript(const QString &text)
{
    QString normalized;
    for (const auto ch : text) {
        if (ch.isLetterOrNumber()) normalized += ch.toCaseFolded();
    }
    return normalized;
}

int lastVoicedFrameEndMs(const QByteArray &pcm)
{
    for (qsizetype offset = pcm.size() - 2; offset >= 0; offset -= 2) {
        const int sample = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(pcm.constData() + offset));
        if (std::abs(sample) > 400) return int(offset / (PcmBytesPerMs * 10) + 1) * 10;
    }
    return 0;
}

enum class Sound { Silence, QuietNoise, Speech, Noise, SpeechNoise, Switching };
struct Phase { const char *name; Sound sound; };
constexpr Phase Phases[] = {
    {"initial_silence", Sound::Silence},
    {"quiet_noise", Sound::QuietNoise},
    {"dual_speech", Sound::Speech},
    {"audible_noise", Sound::Noise},
    {"speech_plus_noise", Sound::SpeechNoise},
    {"speech_silence_switching", Sound::Switching},
    {"silence_recovery", Sound::Silence},
    {"audible_noise_repeat", Sound::Noise},
    {"dual_speech_repeat", Sound::Speech},
    {"final_silence", Sound::Silence},
};

int activeSeconds()
{
    if (qEnvironmentVariableIsSet("CHUNFE_REAL_ASR_SPK_WAV"))
        return setting("CHUNFE_SOAK_SPEECH_SECONDS", 600, SpeechCycleMs / 1000, 3600);
    return int(std::size(Phases)) * setting("CHUNFE_SOAK_PHASE_SECONDS", 60, 1, 3600);
}

struct SpeechTrack {
    QByteArray pcm;
    QString expected;
    QString normalizedExpected;
    int voicedEndMs = 0;
    int startMs(bool finalCycle) const
    {
        // Leave the final utterance active when capture stops. The normal one
        // second quiet endpoint must not preempt the StreamEnd tail flush.
        return finalCycle ? SpeechCycleMs - BlockMs - voicedEndMs : SpeechStartMs;
    }
    QByteArray blockAt(int cycleOffsetMs, bool finalCycle) const
    {
        QByteArray output(FramesPerBlock * 2, '\0');
        const int relativeBytes = (cycleOffsetMs - startMs(finalCycle)) * PcmBytesPerMs;
        const int inputOffset = std::max(0, relativeBytes);
        const int outputOffset = std::max(0, -relativeBytes);
        const qsizetype copyBytes = std::min(pcm.size() - inputOffset, output.size() - outputOffset);
        if (copyBytes > 0)
            std::copy_n(pcm.constData() + inputOffset, copyBytes, output.data() + outputOffset);
        return output;
    }
};

struct SpeechCycleResult {
    QStringList capsules;
    qint64 firstTextAtMs = -1;
    qint64 lastFinalAtMs = -1;
    bool lastFinalAfterStop = false;
    bool hasOpenCapsule = false;
};

struct SpeechResults {
    SpeechCycleResult cycle;
    qint64 lastFinalId = -1;
    int finalizedCapsules = 0;
    std::vector<double> firstTextMs;
    std::vector<double> finalTextMs;
    double stopToFinalMs = -1;
};

QByteArray block(Sound sound, int source, qint64 frameOffset, const QByteArray &speech, quint32 &random)
{
    QByteArray pcm(FramesPerBlock * 2, '\0');
    const qint64 speechFrames = speech.size() / 2;
    for (int i = 0; i < FramesPerBlock; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const int noise = int(random % 2401) - 1200;
        const auto speechIndex = (frameOffset + i + (source == 2 ? speechFrames / 3 : 0)) % speechFrames;
        const int voice = qFromLittleEndian<qint16>(reinterpret_cast<const uchar *>(speech.constData() + speechIndex * 2));
        int sample = 0;
        switch (sound) {
        case Sound::Silence: break;
        case Sound::QuietNoise: sample = noise / 10; break;
        case Sound::Speech: sample = voice; break;
        case Sound::Noise: sample = noise; break;
        case Sound::SpeechNoise: sample = voice + noise / 2; break;
        case Sound::Switching:
            sample = ((frameOffset + i) / (SampleRate * 4)) % 2 == 0 ? voice : 0;
            break;
        }
        qToLittleEndian<qint16>(qint16(std::clamp(sample, -32768, 32767)), reinterpret_cast<uchar *>(pcm.data() + i * 2));
    }
    return pcm;
}
}

// This intentionally bypasses physical capture devices, while exercising the
// real controller, two-source capture signals, bundled runtime and GPU model.
// The optional small animated window measures frame submission intervals;
// neither that nor heartbeat latency establishes subjective responsiveness.
// Run the companion observer for independent system telemetry.
class OfflineTranscriptionSoakTests : public QObject {
    Q_OBJECT
public:
    static void initMain()
    {
        // QtTest otherwise aborts a single test function after five minutes.
        // QTEST_MAIN calls initMain before constructing the application or
        // starting the watchdog. Keep an explicitly supplied timeout intact.
        if (qEnvironmentVariableIsSet("CHUNFE_RUN_ASR_SOAK") &&
            !qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT")) {
            const qint64 activeMs = qint64(activeSeconds()) * 1000;
            const qint64 idleMs = 3LL * setting("CHUNFE_SOAK_IDLE_SECONDS", 10, 1, 60) * 1000;
            qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number(activeMs + idleMs + 300000));
        }
    }

private slots:
    void sustainedDualChannelSilenceNoiseAndSpeech()
    {
        if (!qEnvironmentVariableIsSet("CHUNFE_RUN_ASR_SOAK"))
            QSKIP("Set CHUNFE_RUN_ASR_SOAK=1, CHUNFE_REAL_ASR_MODEL and CHUNFE_REAL_ASR_WAV for the opt-in GPU soak test");
        const auto model = qEnvironmentVariable("CHUNFE_REAL_ASR_MODEL");
        const auto fixture = qEnvironmentVariable("CHUNFE_REAL_ASR_WAV");
        QVERIFY2(!model.isEmpty() && !fixture.isEmpty(), "model and WAV paths are required");
        const auto speech = readPcm(fixture);
        QVERIFY2(speech.size() >= SampleRate * 2, "fixture must contain at least one second of mono 16 kHz PCM16 audio");

        const bool distinctSpeech = qEnvironmentVariableIsSet("CHUNFE_REAL_ASR_SPK_WAV");
        const int totalSeconds = activeSeconds();
        const int phaseSeconds = distinctSpeech ? totalSeconds : setting("CHUNFE_SOAK_PHASE_SECONDS", 60, 1, 3600);
        const int cycles = distinctSpeech ? totalSeconds * 1000 / SpeechCycleMs : 0;
        std::array<SpeechTrack, 2> tracks;
        if (distinctSpeech) {
            QCOMPARE(totalSeconds * 1000 % SpeechCycleMs, 0);
            tracks[0].pcm = speech;
            tracks[1].pcm = readPcm(qEnvironmentVariable("CHUNFE_REAL_ASR_SPK_WAV"));
            tracks[0].expected = qEnvironmentVariable("CHUNFE_SOAK_MIC_EXPECTED");
            tracks[1].expected = qEnvironmentVariable("CHUNFE_SOAK_SPK_EXPECTED");
            for (auto &track : tracks) {
                QVERIFY2(track.pcm.size() >= SampleRate * 2, "each WAV must contain at least one second of mono 16 kHz PCM16");
                track.normalizedExpected = normalizedTranscript(track.expected);
                QVERIFY2(!track.normalizedExpected.isEmpty(), "isolated reference transcript required for each WAV");
                track.voicedEndMs = lastVoicedFrameEndMs(track.pcm);
                QVERIFY2(track.voicedEndMs > 0 && SpeechStartMs + track.voicedEndMs + 1000 + BlockMs <= SpeechCycleMs,
                         "speech must fit with a pre-roll, endpoint silence and settling time in each six second cycle");
            }
            QVERIFY2(tracks[0].pcm != tracks[1].pcm, "MIC and SPK fixtures must differ");
            QVERIFY2(tracks[0].normalizedExpected != tracks[1].normalizedExpected,
                     "MIC and SPK isolated reference transcripts must differ");
        }
        const int idleSeconds = setting("CHUNFE_SOAK_IDLE_SECONDS", 10, 1, 60);
        const int maxFootprint = setting("CHUNFE_SOAK_MAX_FOOTPRINT_MIB", 4096, 1024, 16384);
        const QString logPath = qEnvironmentVariable("CHUNFE_SOAK_JSONL", QStringLiteral("offline-transcription-soak.jsonl"));
        QFile log(logPath);
        QVERIFY2(log.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(log.errorString()));
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("ChunfeOfflineSoakTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Controller"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        AudioPiplineManager manager;
        OfflineTranscriptionController c(manager);
        const auto cleanup = qScopeGuard([&] { c.m_captureId.clear(); c.close(); });
        QElapsedTimer wall; wall.start();
        QString phase = QStringLiteral("baseline_unloaded");
        QString abortReason;
        std::vector<double> heartbeatMs;
        std::vector<double> allHeartbeatMs;
        double peakFootprint = 0;
        int peakPending[2] = {0, 0};
        int severeStalls = 0;
        qint64 framesFed = 0;
        qint64 inferenceCalls[2] = {0, 0};
        qint64 inferenceAudioBytes[2] = {0, 0};
        std::vector<double> inferenceMs;
        std::vector<double> allInferenceMs;
        const bool renderEnabled = qEnvironmentVariableIsSet("CHUNFE_SOAK_RENDER_PROBE");
        qint64 renderedFrames = 0;
        qint64 lastFrameNs = 0;
        std::vector<double> frameIntervals;
        std::vector<double> phaseFrameIntervals;
        std::vector<double> allFrameIntervals;
        qint64 lastBeat = wall.elapsed();
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(50);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now = wall.elapsed();
            const double delay = double(std::max<qint64>(0, now - lastBeat - 50));
            heartbeatMs.push_back(delay); allHeartbeatMs.push_back(delay);
            if (delay > 2000 && ++severeStalls >= 3)
                abortReason = QStringLiteral("three Qt heartbeat delays exceeded two seconds");
            lastBeat = now;
        });
        heartbeat.start();
        auto write = [&](QJsonObject row) {
            row.insert(QStringLiteral("elapsed_ms"), wall.elapsed());
            row.insert(QStringLiteral("utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            row.insert(QStringLiteral("phase"), phase);
            log.write(QJsonDocument(row).toJson(QJsonDocument::Compact)); log.write("\n"); log.flush();
        };
        bool completed = false;
        const auto outcome = qScopeGuard([&] {
            write({{"type", "outcome"}, {"completed", completed}, {"test_failed", QTest::currentTestFailed()},
                   {"audio_seconds_fed_per_source", double(framesFed) / SampleRate},
                   {"abort_reason", abortReason}, {"controller_error", c.errorMessage()}, {"engine_error", c.engineError()}});
        });
        QObject metricsContext;
        std::array<SpeechResults, 2> speechResults;
        int activeSpeechCycle = -1;
        qint64 cycleStartMs = 0;
        qint64 stopRequestedMs = -1;
        if (distinctSpeech) {
            connect(&c, &OfflineTranscriptionController::changed, &metricsContext, [&] {
                if (activeSpeechCycle < 0 || !abortReason.isEmpty()) return;
                const auto now = wall.elapsed();
                for (auto &result : speechResults) result.cycle.hasOpenCapsule = false;
                for (const auto &item : c.lines()) {
                    const auto line = item.toMap();
                    const auto id = line.value(QStringLiteral("id")).toLongLong();
                    const auto source = line.value(QStringLiteral("source")).toString();
                    if (source != QStringLiteral("mic") && source != QStringLiteral("spk")) {
                        abortReason = QStringLiteral("unexpected source on capsule %1").arg(id);
                        return;
                    }
                    const int index = source == QStringLiteral("mic") ? 0 : 1;
                    auto &result = speechResults[index];
                    auto &cycle = result.cycle;
                    if (line.value(QStringLiteral("incomplete")).toBool()) {
                        abortReason = QStringLiteral("incomplete %1 capsule in cycle %2").arg(source).arg(activeSpeechCycle + 1);
                        return;
                    }
                    const bool final = line.value(QStringLiteral("final")).toBool();
                    if (final && id <= result.lastFinalId) continue;
                    const auto text = line.value(QStringLiteral("text")).toString();
                    if (cycle.firstTextAtMs < 0 && !text.isEmpty()) cycle.firstTextAtMs = now;
                    if (!final) {
                        cycle.hasOpenCapsule = true;
                        continue;
                    }
                    if (normalizedTranscript(text).isEmpty()) {
                        abortReason = QStringLiteral("empty final %1 capsule in cycle %2").arg(source).arg(activeSpeechCycle + 1);
                        return;
                    }
                    result.lastFinalId = id;
                    cycle.capsules.push_back(text);
                    cycle.lastFinalAtMs = now;
                    cycle.lastFinalAfterStop = stopRequestedMs >= 0;
                }
            });
        }
        auto verifySpeechCycle = [&] {
            const bool finalCycle = activeSpeechCycle == cycles - 1;
            for (size_t i = 0; i < speechResults.size(); ++i) {
                auto &result = speechResults[i];
                auto &cycle = result.cycle;
                const QString source = i == 0 ? QStringLiteral("mic") : QStringLiteral("spk");
                const auto start = cycleStartMs + tracks[i].startMs(finalCycle);
                const auto text = cycle.capsules.join(QLatin1Char(' '));
                const auto firstTextMs = cycle.firstTextAtMs - start;
                const auto lastFinalMs = cycle.lastFinalAtMs - start - tracks[i].voicedEndMs;
                write({{"type", "utterance"}, {"cycle", activeSpeechCycle + 1}, {"source", source},
                       {"capsule_id", result.lastFinalId}, {"capsule_count", int(cycle.capsules.size())},
                       {"capsules", QJsonArray::fromStringList(cycle.capsules)},
                       {"text", text}, {"first_text_ms", firstTextMs}, {"voiced_end_to_final_ms", lastFinalMs},
                       {"stream_end", cycle.lastFinalAfterStop}});
                if (normalizedTranscript(text) != tracks[i].normalizedExpected) {
                    abortReason = QStringLiteral("%1 cycle %2 transcript mismatch. Expected '%3', received '%4'")
                                      .arg(source).arg(activeSpeechCycle + 1).arg(tracks[i].expected, text);
                    return false;
                }
                if (cycle.hasOpenCapsule || firstTextMs < 0 || cycle.lastFinalAtMs < cycle.firstTextAtMs) {
                    abortReason = QStringLiteral("invalid %1 capsule state or timing in cycle %2").arg(source).arg(activeSpeechCycle + 1);
                    return false;
                }
                if (finalCycle && !cycle.lastFinalAfterStop) {
                    abortReason = QStringLiteral("final %1 capsule was closed before StreamEnd").arg(source);
                    return false;
                }
                result.finalizedCapsules += int(cycle.capsules.size());
                result.firstTextMs.push_back(double(firstTextMs));
                if (finalCycle) result.stopToFinalMs = double(cycle.lastFinalAtMs - stopRequestedMs);
                else result.finalTextMs.push_back(double(lastFinalMs));
                cycle = {};
            }
            return true;
        };
        connect(&c, &OfflineTranscriptionController::inferenceCompleted, &metricsContext,
                [&](int source, qint64 microseconds, int audioBytes) {
            if (source < 1 || source > 2) return;
            ++inferenceCalls[source - 1];
            inferenceAudioBytes[source - 1] += audioBytes;
            const double ms = double(microseconds) / 1000.0;
            inferenceMs.push_back(ms); allInferenceMs.push_back(ms);
        });
        std::unique_ptr<RenderProbe> renderProbe;
        if (renderEnabled) {
            renderProbe = std::make_unique<RenderProbe>();
            const auto error = renderProbe->start([&](qint64 now) {
                ++renderedFrames;
                if (lastFrameNs != 0) {
                    const double interval = double(now - lastFrameNs) / 1000000.0;
                    appendBounded(frameIntervals, interval);
                    appendBounded(phaseFrameIntervals, interval);
                    appendBounded(allFrameIntervals, interval);
                }
                lastFrameNs = now;
            });
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
        auto sample = [&] {
            const auto usage = resources();
            peakFootprint = std::max(peakFootprint, usage.footprintMiB);
            for (int source = 0; source < 2; ++source) peakPending[source] = std::max(peakPending[source], c.m_pendingBytes[source]);
            write({{"type", "sample"}, {"physical_footprint_mib", usage.footprintMiB}, {"cpu_seconds", usage.cpuSeconds},
                   {"mic_pending_bytes", c.m_pendingBytes[0]}, {"spk_pending_bytes", c.m_pendingBytes[1]},
                   {"capsules_created", c.m_sequence}, {"visible_capsules", c.lines().size()},
                   {"heartbeat_p95_ms", percentile(heartbeatMs, .95)}, {"heartbeat_max_ms", percentile(heartbeatMs, 1)},
                   {"mic_inference_calls", inferenceCalls[0]}, {"spk_inference_calls", inferenceCalls[1]},
                   {"mic_inference_audio_bytes", inferenceAudioBytes[0]}, {"spk_inference_audio_bytes", inferenceAudioBytes[1]},
                   {"inference_p95_ms", percentile(inferenceMs, .95)}, {"inference_max_ms", percentile(inferenceMs, 1)},
                   {"rendered_frames", renderedFrames}, {"frame_interval_p95_ms", percentile(frameIntervals, .95)},
                   {"frame_interval_max_ms", percentile(frameIntervals, 1)},
                   {"render_api", renderProbe ? renderProbe->graphicsApi() : QStringLiteral("disabled")},
                   {"mic_failed", c.m_sourceFailed[0]}, {"spk_failed", c.m_sourceFailed[1]}, {"error", c.errorMessage()}});
            heartbeatMs.clear();
            inferenceMs.clear();
            frameIntervals.clear();
            if (usage.footprintMiB > maxFootprint)
                abortReason = QStringLiteral("process physical footprint exceeded %1 MiB").arg(maxFootprint);
        };
        QTimer sampling;
        sampling.setInterval(1000);
        connect(&sampling, &QTimer::timeout, this, sample);
        sampling.start();
        auto waitUntil = [&](const std::function<bool()> &done, int timeoutMs) {
            QElapsedTimer deadline; deadline.start();
            while (!done() && deadline.elapsed() < timeoutMs && abortReason.isEmpty()) QTest::qWait(10);
            return done() && abortReason.isEmpty();
        };
        auto idle = [&] {
            QElapsedTimer timer; timer.start();
            return waitUntil([&] { return timer.elapsed() >= idleSeconds * 1000; }, idleSeconds * 1000 + 1000);
        };
        auto beginPhase = [&](const QString &name) {
            phase = name;
            phaseFrameIntervals.clear();
            write({{"type", "phase_start"}});
            qInfo().noquote() << "SOAK phase:" << phase << "elapsed seconds:" << wall.elapsed() / 1000;
        };
        write({{"type", "configuration"}, {"pid", QCoreApplication::applicationPid()}, {"phase_seconds", phaseSeconds},
               {"active_seconds", totalSeconds}, {"runtime", c.m_runtimePath},
               {"function_timeout_ms", qEnvironmentVariableIntValue("QTEST_FUNCTION_TIMEOUT")},
               {"distinct_speech", distinctSpeech}, {"speech_cycles", cycles},
               {"mic_expected", tracks[0].expected}, {"spk_expected", tracks[1].expected},
               {"noise_peak_pcm16", 1200}, {"quiet_noise_peak_pcm16", 120}, {"footprint_abort_mib", maxFootprint},
               {"physical_capture", false}, {"input_transport", "injected_capture_signals"},
               {"rendering_measured", renderEnabled},
               {"render_measurement", "afterFrameEnd submission intervals, not GPU completion or presentation"}});
        sample();
        QVERIFY2(idle(), qPrintable(abortReason));
        if (renderEnabled) {
            QVERIFY2(renderedFrames >= 10, "render probe did not produce frames during the unloaded baseline");
#if defined(Q_OS_MACOS)
            QCOMPARE(renderProbe->graphicsApi(), QStringLiteral("Metal"));
#endif
        }
        beginPhase(QStringLiteral("load_engine"));
        c.saveModelPath(model);
        c.loadEngine();
        QVERIFY2(waitUntil([&] { return c.engineStatus() != QStringLiteral("starting"); }, 120000), qPrintable(abortReason));
        QVERIFY2(c.engineStatus() == QStringLiteral("ready"), qPrintable(c.engineError()));
        beginPhase(QStringLiteral("loaded_idle"));
        QVERIFY2(idle(), qPrintable(abortReason));

        c.setDevices(QStringLiteral("soak-mic"), QStringLiteral("soak-spk"));
        c.m_opened = true;
        c.startTest();
        // Prevent the asynchronous startup callback from requesting OS capture.
        c.m_status = QStringLiteral("running");
        const auto workerReady = std::make_shared<std::atomic_bool>(false);
        QVERIFY(QMetaObject::invokeMethod(c.m_workerContext, [workerReady] { workerReady->store(true); }));
        QVERIFY2(waitUntil([&] { return workerReady->load(); }, 30000), qPrintable(abortReason));
        const CaptureId id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.m_captureId = id;
        quint32 random[2] = {0x4a35cb61, 0xb746e102};
        qint64 frameOffset = 0;
        const auto activeStart = wall.elapsed();
        const Phase distinctPhase{"distinct_dual_speech", Sound::Speech};
        const auto phases = distinctSpeech ? std::span<const Phase>(&distinctPhase, 1) : std::span<const Phase>(Phases);
        for (const auto &item : phases) {
            beginPhase(QString::fromLatin1(item.name));
            const auto initialCapsules = c.m_sequence;
            const qint64 initialCalls = inferenceCalls[0] + inferenceCalls[1];
            const qint64 initialFrames = renderedFrames;
            QElapsedTimer pacing; pacing.start();
            for (int chunk = 0; chunk < phaseSeconds * 1000 / BlockMs; ++chunk) {
                const int offsetMs = chunk * BlockMs;
                const qint64 due = qint64(chunk + 1) * BlockMs;
                if (distinctSpeech) {
                    if (offsetMs % SpeechCycleMs == 0) {
                        if (activeSpeechCycle >= 0) QVERIFY2(verifySpeechCycle(), qPrintable(abortReason));
                        activeSpeechCycle = offsetMs / SpeechCycleMs;
                        cycleStartMs = activeStart + offsetMs;
                    }
                    // Model a capture block becoming available after its last
                    // sample, so a 200 ms input block cannot shorten latency.
                    QVERIFY2(waitUntil([&] { return wall.elapsed() >= activeStart + due; }, BlockMs + 1000), qPrintable(abortReason));
                }
                for (int source = 1; source <= 2; ++source) {
                    const auto pcm = distinctSpeech
                        ? tracks[source - 1].blockAt(offsetMs % SpeechCycleMs, activeSpeechCycle == cycles - 1)
                        : block(item.sound, source, frameOffset, speech, random[source - 1]);
                    emit manager.pcmReady(id, 0, AudioSource(source),
                                          {pcm, quint64(frameOffset / FramesPerBlock), frameOffset * 1000000000LL / SampleRate});
                }
                frameOffset += FramesPerBlock;
                framesFed += FramesPerBlock;
                if (!distinctSpeech)
                    QVERIFY2(waitUntil([&] { return pacing.elapsed() >= due; }, BlockMs + 1000), qPrintable(abortReason));
                QVERIFY2(!c.m_sourceFailed[0] && !c.m_sourceFailed[1], qPrintable(c.errorMessage()));
                QVERIFY2(abortReason.isEmpty(), qPrintable(abortReason));
                for (int source = 0; source < 2; ++source) peakPending[source] = std::max(peakPending[source], c.m_pendingBytes[source]);
            }
            sample();
            write({{"type", "phase_end"}, {"new_capsules", c.m_sequence - initialCapsules},
                   {"completed_inference_calls", inferenceCalls[0] + inferenceCalls[1] - initialCalls},
                   {"rendered_frames", renderedFrames - initialFrames},
                   {"frame_interval_p95_ms", percentile(phaseFrameIntervals, .95)},
                   {"frame_interval_max_ms", percentile(phaseFrameIntervals, 1)}});
            if (renderEnabled) QVERIFY2(renderedFrames > initialFrames, "render probe produced no frames during a phase");
            if (phase == QStringLiteral("initial_silence") || phase == QStringLiteral("quiet_noise")) {
                QCOMPARE(c.m_sequence, initialCapsules);
                QCOMPARE(c.m_pendingBytes[0], 0);
                QCOMPARE(c.m_pendingBytes[1], 0);
                QCOMPARE(inferenceCalls[0] + inferenceCalls[1], initialCalls);
            }
        }
        const auto activeWallMs = wall.elapsed() - activeStart;
        beginPhase(QStringLiteral("drain"));
        c.m_status = QStringLiteral("stopping");
        c.m_stopTimer.start(5000);
        stopRequestedMs = wall.elapsed();
        emit manager.pcmEnded(id, 0, AudioSource::Mic, 0);
        emit manager.pcmEnded(id, 0, AudioSource::Spk, 0);
        CaptureSnapshot snapshot;
        snapshot.captureId = id;
        emit manager.captureChanged(snapshot);
        QVERIFY2(waitUntil([&] { return c.status() == QStringLiteral("stopped"); }, 10000), qPrintable(abortReason));
        QVERIFY2(c.errorMessage().isEmpty(), qPrintable(c.errorMessage()));
        QVERIFY(c.m_sequence > 0);
        if (distinctSpeech) {
            QVERIFY2(verifySpeechCycle(), qPrintable(abortReason));
            for (size_t i = 0; i < speechResults.size(); ++i) {
                const auto &result = speechResults[i];
                QCOMPARE(result.firstTextMs.size(), size_t(cycles));
                QCOMPARE(result.finalTextMs.size(), size_t(cycles - 1));
                QVERIFY(result.stopToFinalMs >= 0);
                write({{"type", "speech_summary"}, {"source", i == 0 ? "mic" : "spk"},
                       {"finalized_utterances", int(result.firstTextMs.size())},
                       {"finalized_capsules", result.finalizedCapsules},
                       {"first_text_p50_ms", percentile(result.firstTextMs, .50)},
                       {"first_text_p95_ms", percentile(result.firstTextMs, .95)},
                       {"first_text_max_ms", percentile(result.firstTextMs, 1)},
                       {"voiced_end_to_final_p50_ms", percentile(result.finalTextMs, .50)},
                       {"voiced_end_to_final_p95_ms", percentile(result.finalTextMs, .95)},
                       {"voiced_end_to_final_max_ms", percentile(result.finalTextMs, 1)},
                       {"stream_end_to_final_ms", result.stopToFinalMs}});
            }
        }
        activeSpeechCycle = -1;
        c.m_captureId.clear(); c.close();
        beginPhase(QStringLiteral("unload_engine"));
        c.unloadEngine();
        QVERIFY2(waitUntil([&] { return c.engineStatus() == QStringLiteral("stopped"); }, 30000), qPrintable(abortReason));
        beginPhase(QStringLiteral("unloaded_recovery"));
        QVERIFY2(idle(), qPrintable(abortReason));
        sample();
        write({{"type", "summary"}, {"completed", true}, {"active_seconds", totalSeconds},
               {"active_wall_ms", activeWallMs}, {"audio_seconds_fed_per_source", double(framesFed) / SampleRate},
               {"peak_physical_footprint_mib", peakFootprint}, {"final_physical_footprint_mib", resources().footprintMiB},
               {"mic_peak_pending_bytes", peakPending[0]}, {"spk_peak_pending_bytes", peakPending[1]},
               {"heartbeat_p95_ms", percentile(allHeartbeatMs, .95)}, {"heartbeat_max_ms", percentile(allHeartbeatMs, 1)},
               {"inference_p95_ms", percentile(allInferenceMs, .95)}, {"inference_max_ms", percentile(allInferenceMs, 1)},
               {"mic_inference_calls", inferenceCalls[0]}, {"spk_inference_calls", inferenceCalls[1]},
               {"rendered_frames", renderedFrames}, {"stored_frame_intervals", qint64(allFrameIntervals.size())},
               {"frame_interval_p95_ms", percentile(allFrameIntervals, .95)},
               {"frame_interval_max_ms", percentile(allFrameIntervals, 1)},
               {"severe_heartbeat_stalls", severeStalls}});
        completed = true;
        qInfo().noquote() << "SOAK complete:" << logPath;
    }
};

QTEST_MAIN(OfflineTranscriptionSoakTests)
#include "offline_transcription_soak_tests.moc"
