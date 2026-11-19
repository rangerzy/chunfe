#pragma once

#include "deepseek_client.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

class DeepSeekStream : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekStream(QNetworkAccessManager &network, QObject *parent = nullptr);
    ~DeepSeekStream() override;

    bool start(const QString &apiKey, const QString &model, const QJsonArray &messages, QJsonObject options = {});
    void cancel();

signals:
    void textReceived(const QString &text);
    void finished(bool success, const QString &message);

private:
    enum class StreamResult { Receiving, Done, Invalid, ProviderError };

    void readReply(QNetworkReply *reply);
    StreamResult parseStream(QString &text);
    StreamResult takeEvent(QString &text);
    void completeStream(StreamResult result);
    void finishReply(QNetworkReply *reply);
    void complete(bool success, const QString &message = {});
    static QString failureMessage(const QNetworkReply &reply, int status);

    DeepSeekClient m_client;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QByteArray m_streamBuffer;
    QByteArray m_eventData;
    QString m_finishReason;
    bool m_hasText = false;
};
