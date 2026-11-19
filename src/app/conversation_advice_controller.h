#pragma once

#include "conversation_advice_text.h"
#include "deepseek_stream.h"

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>

#include <optional>

class ModelConnectionController;
class QNetworkAccessManager;

class ConversationAdviceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString question READ question NOTIFY uiChanged)
    Q_PROPERTY(QString triggerKind READ triggerKind NOTIFY uiChanged)
    Q_PROPERTY(QString answer READ answer NOTIFY uiChanged)
    Q_PROPERTY(bool generating READ generating NOTIFY uiChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY uiChanged)

public:
    explicit ConversationAdviceController(QString configFilePath, ModelConnectionController &model,
                                          QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);

    QString question() const { return m_question; }
    QString triggerKind() const { return m_triggerKind; }
    QString answer() const { return m_answer; }
    bool generating() const { return m_generating; }
    QString statusMessage() const { return m_statusMessage; }

    Q_INVOKABLE void beginSession(const QString &background);
    Q_INVOKABLE void considerTranscript(const QVariantList &lines);
    Q_INVOKABLE void setListening(bool listening);
    Q_INVOKABLE QVariantList finishSession();

signals:
    void uiChanged();

private:
    struct Candidate {
        QJsonObject payload;
        QString time;
    };

    void startNext();
    void publishAdvice();
    void completeRequest(bool success, const QString &message);
    void appendAdvice(bool incomplete);

    const QString m_configFilePath;
    ModelConnectionController &m_model;
    DeepSeekStream m_stream;
    QSet<QString> m_seenIds;
    QList<Candidate> m_pending;
    std::optional<Candidate> m_current;
    ConversationAdviceText::Response m_response;
    QVariantList m_records;
    QString m_apiKey;
    QString m_modelName;
    QString m_promptTemplate;
    QString m_sessionBackground;
    QString m_question;
    QString m_triggerKind;
    QString m_answer;
    QString m_statusMessage;
    bool m_active = false;
    bool m_listening = false;
    bool m_generating = false;
};
