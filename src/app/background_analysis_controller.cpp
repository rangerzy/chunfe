#include "background_analysis_controller.h"

#include "app_settings.h"
#include "background_controller.h"
#include "model_connection_controller.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>

BackgroundAnalysisController::BackgroundAnalysisController(const QString &configFilePath, BackgroundController &document,
                                                           ModelConnectionController &model, QNetworkAccessManager *network,
                                                           QObject *parent)
    : QObject(parent),
      m_configFilePath(configFilePath),
      m_document(document),
      m_model(model),
      m_stream(network ? *network : *new QNetworkAccessManager(this))
{
    AppSettings::backgroundAnalysisPrompt(m_configFilePath);
    connect(&m_model, &ModelConnectionController::modelConfiguredChanged, this,
            &BackgroundAnalysisController::configuredChanged);
    connect(&m_document, &BackgroundController::backgroundTextChanged, this, [this] {
        m_stream.cancel();
        resetAnalysis(false);
    });
    connect(&m_stream, &DeepSeekStream::textReceived, this, [this](const QString &text) {
        m_resultText += text;
        emit resultTextChanged();
    });
    connect(&m_stream, &DeepSeekStream::finished, this, [this](bool success, const QString &message) {
        setStatus(false, !success, message);
    });
}

bool BackgroundAnalysisController::configured() const
{
    return m_model.modelConfigured();
}

bool BackgroundAnalysisController::startAnalysis()
{
    const QString background = m_document.backgroundText();
    if (m_running || !configured() || background.isEmpty())
        return false;

    QString prompt = AppSettings::backgroundAnalysisPrompt(m_configFilePath);
    if (prompt.isNull()) {
        setStatus(false, true, tr("无法读取或保存分析提示词，请检查 config.ini。"));
        return false;
    }
    if (!prompt.contains(AppSettings::BackgroundAnalysisPlaceholder)) {
        setStatus(false, true, tr("请在 config.ini 的 backgroundAnalysis/prompt 中保留 {{background}} 占位符。"));
        return false;
    }
    prompt.replace(QString(AppSettings::BackgroundAnalysisPlaceholder), background);

    const QJsonArray messages{QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), prompt}
    }};
    const QJsonObject options{
        {QStringLiteral("max_tokens"), 2048},
        {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}}}
    };
    if (!m_stream.start(m_model.savedApiKey(), m_model.savedModelName(), messages, options)) {
        setStatus(false, true, tr("无法开始分析，请检查 DeepSeek 模型配置后重试。"));
        return false;
    }

    resetAnalysis(true);
    return true;
}

void BackgroundAnalysisController::cancelAnalysis()
{
    if (!m_running)
        return;

    m_stream.cancel();
    setStatus(false, false, tr("已停止分析。"));
}

void BackgroundAnalysisController::setStatus(bool running, bool failed, const QString &message)
{
    if (m_running == running && m_failed == failed && m_statusMessage == message)
        return;
    m_running = running;
    m_failed = failed;
    m_statusMessage = message;
    emit statusChanged();
}

void BackgroundAnalysisController::resetAnalysis(bool running)
{
    const bool hadResult = !m_resultText.isEmpty();
    m_resultText.clear();
    setStatus(running, false);
    if (hadResult)
        emit resultTextChanged();
}
