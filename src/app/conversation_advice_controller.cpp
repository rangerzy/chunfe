#include "conversation_advice_controller.h"

#include "app_settings.h"
#include "model_connection_controller.h"

#include <QJsonArray>
#include <QNetworkAccessManager>

#include <utility>

namespace {
constexpr qsizetype MaxQueuedCandidates = 4;
constexpr qsizetype MaxContextLines = 24;
constexpr qsizetype MaxContextCharacters = 8000;

bool confirmed(const QVariantMap &line)
{
    return line.value(QStringLiteral("final")).toBool() && !line.value(QStringLiteral("incomplete")).toBool();
}

QJsonObject attributedLine(const QString &source, const QString &time, const QString &text)
{
    return {{QStringLiteral("source"), source}, {QStringLiteral("time"), time}, {QStringLiteral("text"), text}};
}

QJsonArray recentContext(const QVariantList &lines, qsizetype candidateRow)
{
    QJsonArray context;
    qsizetype remaining = MaxContextCharacters;
    for (qsizetype i = candidateRow; i > 0 && context.size() < MaxContextLines && remaining > 0;) {
        const auto line = lines.at(--i).toMap();
        const QString source = line.value(QStringLiteral("source")).toString();
        if (!confirmed(line) || (source != QStringLiteral("spk") && source != QStringLiteral("mic")))
            continue;

        QString text = line.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty())
            continue;
        if (text.size() > remaining) {
            text.truncate(remaining);
            if (text.at(text.size() - 1).isHighSurrogate())
                text.chop(1);
        }
        if (text.isEmpty())
            break;
        remaining -= text.size();
        context.prepend(attributedLine(source, line.value(QStringLiteral("time")).toString(), text));
    }
    return context;
}
}

ConversationAdviceController::ConversationAdviceController(QString configFilePath, ModelConnectionController &model,
                                                           QNetworkAccessManager *network, QObject *parent)
    : QObject(parent),
      m_configFilePath(std::move(configFilePath)),
      m_model(model),
      m_stream(network ? *network : *new QNetworkAccessManager(this), this)
{
    AppSettings::conversationAdvicePrompt(m_configFilePath);
    connect(&m_stream, &DeepSeekStream::textReceived, this, [this](const QString &text) {
        if (!m_current)
            return;
        m_response.append(text);
        publishAdvice();
    });
    connect(&m_stream, &DeepSeekStream::finished, this, &ConversationAdviceController::completeRequest);
}

void ConversationAdviceController::beginSession(const QString &background)
{
    m_stream.cancel();
    m_current.reset();
    m_response = {};
    m_seenIds.clear();
    m_pending.clear();
    m_records.clear();
    m_apiKey = m_model.modelConfigured() ? m_model.savedApiKey() : QString();
    m_modelName = m_model.savedModelName();
    m_sessionBackground = background;
    m_promptTemplate = AppSettings::conversationAdvicePrompt(m_configFilePath);
    m_active = m_listening = true;

    QString status;
    if (m_apiKey.isEmpty()) {
        status = tr("未配置模型，暂不生成回应建议。");
    } else if (m_promptTemplate.isNull()) {
        status = tr("无法读取或保存回应建议提示词，请检查 config.ini。");
    } else if (!ConversationAdviceText::hasPlaceholders(m_promptTemplate)) {
        status = tr("请在 config.ini 的 conversationAdvice/prompt 中保留 {{background}} 和 {{context}}。");
        m_promptTemplate.clear();
    }
    const bool changed = m_generating || !m_question.isEmpty() || !m_triggerKind.isEmpty() || !m_answer.isEmpty()
        || m_statusMessage != status;
    m_statusMessage = status;
    m_question.clear();
    m_triggerKind.clear();
    m_answer.clear();
    m_generating = false;
    if (changed)
        emit uiChanged();
}

void ConversationAdviceController::considerTranscript(const QVariantList &lines)
{
    if (!m_active || m_apiKey.isEmpty() || m_promptTemplate.isEmpty())
        return;

    for (qsizetype row = 0; row < lines.size(); ++row) {
        const auto line = lines.at(row).toMap();
        if (line.value(QStringLiteral("source")).toString() != QStringLiteral("spk") || !confirmed(line))
            continue;

        const QString id = line.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || m_seenIds.contains(id))
            continue;
        m_seenIds.insert(id);
        if (!m_listening)
            continue;

        const QString text = line.value(QStringLiteral("text")).toString();
        if (!ConversationAdviceText::shouldEvaluate(text))
            continue;
        const QString time = line.value(QStringLiteral("time")).toString();
        if (m_pending.size() >= MaxQueuedCandidates)
            m_pending.removeFirst();
        m_pending.append(Candidate{
            {{QStringLiteral("candidate"), attributedLine(QStringLiteral("spk"), time, text)},
             {QStringLiteral("context"), recentContext(lines, row)}},
            time});
        startNext();
    }
}

void ConversationAdviceController::setListening(bool listening)
{
    m_listening = m_active && listening;
    if (!m_listening)
        m_pending.clear();
}

QVariantList ConversationAdviceController::finishSession()
{
    if (!m_active)
        return m_records;

    m_active = m_listening = false;
    m_pending.clear();
    m_stream.cancel();
    if (m_generating)
        appendAdvice(true);
    m_current.reset();
    m_response = {};
    m_seenIds.clear();
    m_apiKey.clear();
    m_modelName.clear();
    m_promptTemplate.clear();
    m_sessionBackground.clear();
    if (std::exchange(m_generating, false))
        emit uiChanged();
    return m_records;
}

void ConversationAdviceController::startNext()
{
    if (!m_active || !m_listening || m_current || m_pending.isEmpty())
        return;

    m_current = m_pending.takeFirst();
    m_response = {};
    const QJsonArray messages{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), ConversationAdviceText::systemPrompt()}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"),
                     ConversationAdviceText::renderPrompt(m_promptTemplate, m_sessionBackground, m_current->payload)}},
    };
    const QJsonObject options{
        {QStringLiteral("max_tokens"), 640},
        {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}}},
    };
    if (!m_stream.start(m_apiKey, m_modelName, messages, options))
        completeRequest(false, tr("无法生成回应建议，请检查模型配置。"));
}

void ConversationAdviceController::publishAdvice()
{
    if (m_response.state() != ConversationAdviceText::Response::State::Advice)
        return;
    if (m_generating && m_question == m_response.question() && m_triggerKind == m_response.kind()
        && m_answer == m_response.answer())
        return;

    m_question = m_response.question();
    m_triggerKind = m_response.kind();
    m_answer = m_response.answer();
    m_statusMessage.clear();
    m_generating = true;
    emit uiChanged();
}

void ConversationAdviceController::completeRequest(bool success, const QString &message)
{
    if (!m_current)
        return;

    m_response.finish();
    const bool wasGenerating = std::exchange(m_generating, false);
    const bool validAdvice = m_response.state() == ConversationAdviceText::Response::State::Advice;
    const bool receivedAdvice = success && validAdvice;
    if (validAdvice && (receivedAdvice || wasGenerating)) {
        m_question = m_response.question();
        m_triggerKind = m_response.kind();
        m_answer = m_response.answer();
    }
    if (receivedAdvice || wasGenerating)
        appendAdvice(!success || !validAdvice || !message.isEmpty());

    QString status = message;
    if (success) {
        if (m_response.state() == ConversationAdviceText::Response::State::Ignored)
            status.clear();
        else if (!validAdvice)
            status = tr("模型未返回可用的回应建议。");
    }
    m_current.reset();
    m_response = {};
    const bool statusChanged = m_statusMessage != status;
    m_statusMessage = std::move(status);
    if (wasGenerating || receivedAdvice || statusChanged)
        emit uiChanged();
    startNext();
}

void ConversationAdviceController::appendAdvice(bool incomplete)
{
    m_records.append(QVariantMap{
        {QStringLiteral("kind"), m_triggerKind}, {QStringLiteral("question"), m_question},
        {QStringLiteral("answer"), m_answer}, {QStringLiteral("incomplete"), incomplete},
        {QStringLiteral("source"), QStringLiteral("spk")}, {QStringLiteral("time"), m_current->time},
    });
}
