#pragma once

#include "offline_transcription_engine.h"

#include <QByteArrayView>
#include <QList>

// Independent speech detection for MIC (1) and SPK (2). Calls must be serialized.
// Input and returned PCM are mono, 16 kHz, signed 16-bit little-endian.
class OFFLINE_TRANSCRIPTION_EXPORT OfflineVoiceActivity {
public:
    struct Batch {
        QByteArray pcm;
        bool final = false;
        // The detector's padded speech-end sample since the last start(), if any.
        qint64 endpointSample = -1;
    };

    OfflineVoiceActivity();
    ~OfflineVoiceActivity();
    OfflineVoiceActivity(const OfflineVoiceActivity &) = delete;
    OfflineVoiceActivity &operator=(const OfflineVoiceActivity &) = delete;

    bool load(const QString &runtimePath, const QString &modelPath, QString *error);
    bool start(int source, QString *error);
    bool push(int source, QByteArrayView pcm, QList<Batch> *batches, QString *error);
    QByteArray takeTail(int source);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
