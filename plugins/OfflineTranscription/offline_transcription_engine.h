#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtCore/qglobal.h>
#include <memory>

#if defined(OFFLINE_TRANSCRIPTION_LIBRARY)
#define OFFLINE_TRANSCRIPTION_EXPORT Q_DECL_EXPORT
#else
#define OFFLINE_TRANSCRIPTION_EXPORT Q_DECL_IMPORT
#endif

// Synchronous interface. A caller must serialize every call on one worker thread.
// Sources: 1 = microphone, 2 = system audio. Audio is 16 kHz mono PCM16 LE.
class OFFLINE_TRANSCRIPTION_EXPORT OfflineTranscriptionEngine {
public:
    static constexpr int WindowBytes = 16000 * 2;
    struct Result {
        // Newly confirmed sentences followed by the full text of the open capsule.
        QStringList sentences;
        QString draft;
        // A short PCM block can be buffered without changing the visible text.
        bool updated = false;
    };

    OfflineTranscriptionEngine();
    ~OfflineTranscriptionEngine();
    OfflineTranscriptionEngine(const OfflineTranscriptionEngine &) = delete;
    OfflineTranscriptionEngine &operator=(const OfflineTranscriptionEngine &) = delete;
    bool load(const QString &runtimePath, const QString &modelPath, QString *error);
    bool start(int source, QString *error);
    bool push(int source, const QByteArray &pcm16, Result *result, QString *error);
    bool finish(int source, Result *result, QString *error);
    void reset(int source);
    void unload();
    bool isLoaded() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
