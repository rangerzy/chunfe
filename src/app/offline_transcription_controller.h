#pragma once

#include <QObject>
#include <QVariantList>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <audio_pipline_manager.h>
#include <offline_transcription_engine.h>
#include <atomic>
#include <memory>

class OfflineVoiceActivity;

class OfflineTranscriptionController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString modelPath READ modelPath NOTIFY changed)
    Q_PROPERTY(QString attemptedModelPath READ attemptedModelPath NOTIFY changed)
    Q_PROPERTY(QString engineStatus READ engineStatus NOTIFY changed)
    Q_PROPERTY(QString engineError READ engineError NOTIFY changed)
    Q_PROPERTY(QObject* testController READ testController CONSTANT)
    Q_PROPERTY(bool opened READ opened NOTIFY openedChanged)
    Q_PROPERTY(bool conversationActive READ conversationActive NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY changed)
    Q_PROPERTY(QString spkDevice READ spkDevice NOTIFY changed)
    Q_PROPERTY(QString micDevice READ micDevice NOTIFY changed)
    Q_PROPERTY(QString spkState READ spkState NOTIFY changed)
    Q_PROPERTY(QString micState READ micState NOTIFY changed)
    Q_PROPERTY(qreal spkLevel READ spkLevel NOTIFY changed)
    Q_PROPERTY(qreal micLevel READ micLevel NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY linesChanged)
    Q_PROPERTY(QVariantList conversationLines READ conversationLines NOTIFY linesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit OfflineTranscriptionController(AudioPiplineManager &manager, QString configFilePath, QObject *parent = nullptr);
    ~OfflineTranscriptionController() override;
    QString modelPath() const { return m_modelPath; }
    QString attemptedModelPath() const { return m_attemptedModelPath; }
    QString engineStatus() const { return m_engineStatus; }
    QString engineError() const { return m_engineError; }
    QObject *testController() { return this; }
    bool opened() const { return m_opened; }
    bool conversationActive() const { return m_conversationActive; }
    QString status() const { return m_status; }
    QString elapsedText() const;
    QString spkDevice() const { return m_spkName; }
    QString micDevice() const { return m_micName; }
    QString spkState() const { return m_spkState; }
    QString micState() const { return m_micState; }
    qreal spkLevel() const { return m_spkLevel; }
    qreal micLevel() const { return m_micLevel; }
    QVariantList lines() const { return m_lines; }
    QVariantList conversationLines() const;
    QString errorMessage() const { return m_error; }
    Q_INVOKABLE QString pathFromUrl(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE void loadEngine(const QString &path);
    Q_INVOKABLE void unloadEngine();
    Q_INVOKABLE void setDevices(const QString &mic, const QString &spk);
    Q_INVOKABLE void open();
    Q_INVOKABLE void close();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void restart();
    Q_INVOKABLE bool startConversation();
    Q_INVOKABLE void stopConversation();
    Q_INVOKABLE void abortConversation();
    Q_INVOKABLE bool setConversationPaused(bool paused);
signals:
    void changed();
    void openedChanged();
    void linesChanged();
    void conversationFinished(const QVariantList &lines);
    // Worker timing, delivered on the controller thread. Finish uses audioBytes=0.
    void inferenceCompleted(int source, qint64 elapsedMicroseconds, int audioBytes);
private:
    friend class OfflineTranscriptionControllerTests;
    friend class OfflineTranscriptionLifecycleTests;
    friend class OfflineTranscriptionSoakTests;
    static QString bundledRuntimePath(const QString &applicationDir);
    bool sessionActive() const { return m_opened || m_conversationActive; }
    qint64 activeElapsedMs() const;
    void startTest();
    void startCaptureSession();
    void failCaptureStart(const QString &message = {});
    void beginCapture();
    void consumeAudio(const AudioPipline::CaptureId &id, std::uint32_t epoch, AudioPipline::AudioSource source,
                      const AudioPipline::CapturedAudio &audio);
    void endAudio(const AudioPipline::CaptureId &id, std::uint32_t epoch, AudioPipline::AudioSource source,
                  std::uint64_t trailingDroppedFrames);
    void submitAudio(int source, QByteArray pcm, bool drainVadTail = false);
    void invalidateSource(int source);
    bool acceptsAudio() const;
    bool acceptsCaptureAudio(const AudioPipline::CaptureId &id, std::uint32_t epoch, AudioPipline::AudioSource source) const;
    void onCapture(const AudioPipline::CaptureSnapshot &snapshot);
    void applyResult(int source, const OfflineTranscriptionEngine::Result &result, bool isFinal, bool incomplete = false);
    bool finishSentence(int source, bool incomplete = false);
    void finishStream(int source, quint64 generation, bool restartStream, bool incomplete = false);
    bool updateLine(int source);
    qsizetype lineIndex(qint64 id) const;
    void clearLines();
    void stopCapture(AudioPipline::StopMode mode);
    void drainSession();
    void discardSession();
    void stopIfDrained();
    void completeStop();
    void finishConversation();
    void finishOpenStreams(bool restartStreams);
    void markIncomplete();
    void failSource(int source, const QString &message);
    void refreshNames();
    struct Worker;
    AudioPiplineManager &m_manager;
    const QString m_configFilePath;
    QThread m_thread;
    QObject *m_workerContext = nullptr;
    std::shared_ptr<Worker> m_worker;
    std::unique_ptr<OfflineVoiceActivity> m_vad;
    QString m_runtimePath, m_vadModelPath, m_modelPath, m_attemptedModelPath;
    QString m_engineStatus = QStringLiteral("stopped"), m_engineError;
    QString m_micId, m_spkId, m_micName, m_spkName, m_micState = QStringLiteral("stopped"), m_spkState = QStringLiteral("stopped");
    QString m_status = QStringLiteral("idle"), m_error, m_captureId, m_permissionRequest;
    qreal m_micLevel = 0, m_spkLevel = 0;
    QVariantList m_lines;
    QString m_pendingText[2];
    qint64 m_lineId[2] = {-1, -1};
    qint64 m_sequence = 0;
    int m_pendingBytes[2] = {0, 0};
    std::shared_ptr<std::atomic_bool> m_cancelled[2] = {
        std::make_shared<std::atomic_bool>(false), std::make_shared<std::atomic_bool>(false)};
    quint64 m_sourceEpoch[2] = {0, 0};
    qint64 m_frozenElapsedMs = -1;
    qint64 m_pauseStartedMs = -1;
    qint64 m_pausedElapsedMs = 0;
    bool m_sourceFailed[2] = {false, false};
    bool m_sourceEnded[2] = {false, false};
    std::uint32_t m_captureEpoch = 0;
    int m_finishing = 0;
    int m_finishingBySource[2] = {0, 0};
    int m_attemptedSources = 0;
    bool m_startedCapture = false;
    quint64 m_generation = 0;
    bool m_opened = false, m_captureEnded = false, m_restartPending = false;
    bool m_conversationActive = false;
    bool m_waitingForCaptureRelease = false;
    bool m_devicesSelected = false;
    QElapsedTimer m_elapsed;
    QTimer m_tick, m_stopTimer;
};
