#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class DeepSeekClient
{
public:
    explicit DeepSeekClient(QNetworkAccessManager &network);

    static QString apiEndpoint();
    static bool isValidApiKey(const QString &apiKey);

    // Returns null for an invalid key. The caller must dispose of the reply after use.
    QNetworkReply *listModels(const QString &apiKey);
    QNetworkReply *chatCompletion(const QString &apiKey, const QString &model,
                                 const QJsonArray &messages, QJsonObject options = {});

private:
    QNetworkAccessManager &m_network;
};
