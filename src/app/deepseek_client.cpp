#include "deepseek_client.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace {
QNetworkRequest authenticatedRequest(const QString &path, const QString &apiKey)
{
    QUrl url(DeepSeekClient::apiEndpoint());
    url.setPath(path);
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + apiKey.toLatin1());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, false);
    return request;
}
}

DeepSeekClient::DeepSeekClient(QNetworkAccessManager &network)
    : m_network(network)
{
}

QString DeepSeekClient::apiEndpoint()
{
    return QStringLiteral("https://api.deepseek.com");
}

bool DeepSeekClient::isValidApiKey(const QString &apiKey)
{
    return !apiKey.isEmpty() && std::ranges::all_of(apiKey, [](QChar character) {
        return character.unicode() >= 0x21 && character.unicode() <= 0x7e;
    });
}

QNetworkReply *DeepSeekClient::listModels(const QString &apiKey)
{
    if (!isValidApiKey(apiKey))
        return nullptr;

    auto request = authenticatedRequest(QStringLiteral("/models"), apiKey);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    return m_network.get(request);
}

QNetworkReply *DeepSeekClient::chatCompletion(const QString &apiKey, const QString &model,
                                             const QJsonArray &messages, QJsonObject options)
{
    if (!isValidApiKey(apiKey))
        return nullptr;

    auto request = authenticatedRequest(QStringLiteral("/chat/completions"), apiKey);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    options.insert(QStringLiteral("model"), model);
    options.insert(QStringLiteral("messages"), messages);
    return m_network.post(request, QJsonDocument(options).toJson(QJsonDocument::Compact));
}
