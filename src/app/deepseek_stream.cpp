#include "deepseek_stream.h"

#include <QJsonDocument>
#include <QNetworkReply>

#include <chrono>
#include <utility>

using namespace std::chrono_literals;

DeepSeekStream::DeepSeekStream(QNetworkAccessManager &network, QObject *parent)
    : QObject(parent),
      m_client(network),
      m_timeout(this)
{
    m_timeout.setSingleShot(true);
    m_timeout.setTimerType(Qt::PreciseTimer);
    m_timeout.setInterval(60s);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        complete(false, tr("等待 DeepSeek 回复超时，请稍后重试。"));
    });
}

DeepSeekStream::~DeepSeekStream()
{
    cancel();
}

bool DeepSeekStream::start(const QString &apiKey, const QString &model, const QJsonArray &messages, QJsonObject options)
{
    if (m_reply || model.trimmed().isEmpty() || messages.isEmpty())
        return false;

    options.insert(QStringLiteral("stream"), true);
    QNetworkReply *reply = m_client.chatCompletion(apiKey, model, messages, std::move(options));
    if (!reply)
        return false;

    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] { readReply(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishReply(reply); });
    m_timeout.start();
    if (reply->isFinished() || reply->bytesAvailable()) {
        // Let the caller finish entering its running state before delivering available data.
        QMetaObject::invokeMethod(this, [this, readyReply = QPointer<QNetworkReply>(reply)] {
            auto *ready = readyReply.data();
            if (!ready || m_reply != ready)
                return;
            if (ready->isFinished())
                finishReply(ready);
            else
                readReply(ready);
        }, Qt::QueuedConnection);
    }

    return true;
}

void DeepSeekStream::cancel()
{
    m_timeout.stop();
    m_streamBuffer.clear();
    m_eventData.clear();
    m_finishReason.clear();
    m_hasText = false;
    if (auto *reply = m_reply.data()) {
        m_reply.clear();
        disconnect(reply, nullptr, this, nullptr);
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
    }
}

void DeepSeekStream::complete(bool success, const QString &message)
{
    cancel();
    emit finished(success, message);
}

void DeepSeekStream::readReply(QNetworkReply *reply)
{
    if (m_reply != reply)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status / 100 != 2 || reply->error() != QNetworkReply::NoError)
        return;

    m_streamBuffer += reply->readAll();
    QString text;
    const StreamResult result = parseStream(text);
    if (!text.isEmpty()) {
        m_timeout.start();
        m_hasText = m_hasText || !text.trimmed().isEmpty();
        emit textReceived(text);
    }
    if (m_reply == reply && result != StreamResult::Receiving)
        completeStream(result);
}

DeepSeekStream::StreamResult DeepSeekStream::parseStream(QString &text)
{
    StreamResult result = StreamResult::Receiving;
    // Keep incomplete lines as raw bytes so chunks can split UTF-8 characters.
    qsizetype offset = 0;
    while (result == StreamResult::Receiving) {
        const qsizetype newline = m_streamBuffer.indexOf('\n', offset);
        if (newline < 0)
            break;
        QByteArrayView line(m_streamBuffer.constData() + offset, newline - offset);
        offset = newline + 1;
        if (line.endsWith('\r'))
            line.chop(1);

        if (line.isEmpty() && !m_eventData.isEmpty()) {
            result = takeEvent(text);
        } else if (line.startsWith("data:")) {
            line = line.sliced(5);
            if (line.startsWith(' '))
                line = line.sliced(1);
            if (!m_eventData.isEmpty())
                m_eventData += '\n';
            m_eventData += line;
        }
    }
    m_streamBuffer.remove(0, offset);
    return result;
}

DeepSeekStream::StreamResult DeepSeekStream::takeEvent(QString &text)
{
    const QByteArray data = m_eventData.trimmed();
    m_eventData.clear();
    if (data == "[DONE]")
        return StreamResult::Done;

    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject())
        return StreamResult::Invalid;
    const auto payload = document.object();
    if (payload.contains(QStringLiteral("error")))
        return StreamResult::ProviderError;

    const auto choices = payload.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const auto choice = choices.first().toObject();
        text += choice.value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
        const QString finishReason = choice.value(QStringLiteral("finish_reason")).toString();
        if (!finishReason.isEmpty())
            m_finishReason = finishReason;
    }
    return StreamResult::Receiving;
}

void DeepSeekStream::completeStream(StreamResult result)
{
    if (result == StreamResult::ProviderError || m_finishReason == QStringLiteral("aborted")
        || m_finishReason == QStringLiteral("insufficient_system_resource")) {
        complete(false, tr("DeepSeek 中断了回复，请稍后重试。"));
    } else if (m_finishReason == QStringLiteral("content_filter")) {
        complete(false, tr("DeepSeek 未能完成回复，请修改输入后重试。"));
    } else if (result == StreamResult::Invalid || !m_hasText
               || (!m_finishReason.isEmpty() && m_finishReason != QStringLiteral("stop")
                   && m_finishReason != QStringLiteral("length"))) {
        complete(false, tr("DeepSeek 未返回有效的文本回复，请重试。"));
    } else if (m_finishReason == QStringLiteral("length")) {
        complete(true, tr("回复已达到长度上限。"));
    } else {
        complete(true);
    }
}

void DeepSeekStream::finishReply(QNetworkReply *reply)
{
    if (m_reply != reply)
        return;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status / 100 != 2 || reply->error() != QNetworkReply::NoError) {
        complete(false, failureMessage(*reply, status));
        return;
    }
    readReply(reply);
    if (m_reply == reply) {
        complete(false, !m_hasText
                         ? tr("DeepSeek 未返回有效的回复，请重试。")
                         : tr("DeepSeek 回复提前结束，请重试。"));
    }
}

QString DeepSeekStream::failureMessage(const QNetworkReply &reply, int status)
{
    switch (status) {
    case 401:
        return tr("DeepSeek API Key 无效，请检查后重试。");
    case 402:
        return tr("DeepSeek 账户余额不足，请充值后重试。");
    case 403:
        return tr("DeepSeek 拒绝了访问，请检查 API Key 的权限。");
    case 429:
        return tr("DeepSeek 请求过于频繁，请稍后重试。");
    }
    if (status / 100 == 3)
        return tr("DeepSeek 返回了重定向，请检查网络配置后重试。");
    if (status / 100 == 4)
        return tr("DeepSeek 拒绝了请求，请检查所选模型和输入内容后重试。");
    if (status >= 500)
        return tr("DeepSeek 服务暂时不可用，请稍后重试。");
    if (reply.error() == QNetworkReply::TimeoutError || reply.error() == QNetworkReply::ProxyTimeoutError)
        return tr("等待 DeepSeek 回复超时，请稍后重试。");
    if (reply.error() == QNetworkReply::SslHandshakeFailedError)
        return tr("无法建立到 DeepSeek 的安全连接，请检查证书和系统时间。");
    return tr("无法连接到 DeepSeek，请检查网络后重试。");
}
