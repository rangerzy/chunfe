#ifndef CHUNFE_AUDIO_LEVEL_TEST_H
#define CHUNFE_AUDIO_LEVEL_TEST_H

#include <QObject>
#include <QString>
#include <audio_pipline_manager.h>

// 页面收音测试的控制器。复用应用的 manager，只处理自己启动的采集会话；
// 测试数据不进入转写或保存，manager 必须比本对象更晚销毁。
class AudioLevelTest final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool stopping READ stopping NOTIFY changed)
    Q_PROPERTY(qreal microphoneLevel READ microphoneLevel NOTIFY changed)
    Q_PROPERTY(qreal systemLevel READ systemLevel NOTIFY changed)
    Q_PROPERTY(QString microphoneStatus READ microphoneStatus NOTIFY changed)
    Q_PROPERTY(QString systemStatus READ systemStatus NOTIFY changed)

public:
    explicit AudioLevelTest(AudioPiplineManager& manager, QObject* parent = nullptr);
    ~AudioLevelTest() override;

    bool active() const { return m_active; }
    bool stopping() const { return m_stopping; }
    qreal microphoneLevel() const { return m_microphoneLevel; }
    qreal systemLevel() const { return m_systemLevel; }
    QString microphoneStatus() const { return m_microphoneStatus; }
    QString systemStatus() const { return m_systemStatus; }

    // ID 来自设备列表；某路为空时只测试另一条。权限和启动结果异步返回。
    Q_INVOKABLE void start(const QString& microphoneId, const QString& systemId);
    // 撤销测试意图并停止本会话；设备释放可能尚未完成，见 stopping。
    Q_INVOKABLE void stop();
    // 设备选择改变时停止正在进行的测试，由用户手动重新开始。
    Q_INVOKABLE void setDevices(const QString& microphoneId, const QString& systemId);

signals:
    void changed();

private:
    void beginCapture();
    void handleRequest(const AudioPipline::RequestId& id, const AudioPipline::RequestResult& result);
    void handleCapture(const AudioPipline::CaptureSnapshot& snapshot);
    void handleIssue(const AudioPipline::AudioIssue& issue);
    void stopInternal(const QString& reason, bool preserveErrors);
    void fail(AudioPipline::AudioSource source, const QString& reason);
    QString userMessage(const AudioPipline::AudioError& error,
                        AudioPipline::AudioSource source) const;
    void setRequestedStatus(const QString& text);

    AudioPiplineManager& m_manager;
    QString m_microphoneId;
    QString m_systemId;
    AudioPipline::CaptureId m_captureId;       // 只接收并停止此测试持有的会话。
    AudioPipline::RequestId m_permissionRequest; // 用于过滤取消后迟到的权限回执。
    AudioPipline::AudioSources m_attempted;    // 本次启动尝试的来源。
    AudioPipline::AudioSources m_failed;       // 本轮已失败的来源，不再重试。
    quint64 m_generation = 0;                  // 停止或换设备后使延迟重试失效。
    QString m_microphoneStatus;
    QString m_systemStatus;
    QString m_afterStopMicrophoneStatus;
    QString m_afterStopSystemStatus;
    qreal m_microphoneLevel = 0;
    qreal m_systemLevel = 0;
    bool m_active = false;   // 用户仍希望测试，包括等待权限、启动及失败回退。
    bool m_stopping = false; // 正在等待旧会话释放；回退期间可与 active 同时为真。
    bool m_hasRun = false;   // 收到 captureStarted 后才视为已成功启动。
    bool m_terminalFailure = false;
    bool m_stopRequested = false;
};

#endif
