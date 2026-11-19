#include "model_connection_controller.h"

#include "app_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>

#include <array>
#include <chrono>
#include <utility>

using namespace std::chrono_literals;

namespace {
const QStringList SupportedModels{QStringLiteral("deepseek-flash"), QStringLiteral("deepseek-v4-pro")};
constexpr QLatin1StringView VerificationDigestKey{"modelConnection/verification/profileDigest"};
constexpr QLatin1StringView VerificationStatusKey{"modelConnection/verification/status"};
constexpr QLatin1StringView VerificationErrorKey{"modelConnection/verification/errorCode"};
constexpr std::array VerificationKeys{VerificationDigestKey, VerificationStatusKey, VerificationErrorKey};

struct VerificationError {
    QStringView code;
    const char *message;
};

constexpr VerificationError VerificationErrors[]{
    {u"invalid_key", QT_TRANSLATE_NOOP("ModelConnectionController", "API Key 无效，请检查后重试。")},
    {u"forbidden", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 拒绝了访问，请检查 API Key 的权限。")},
    {u"rate_limited", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 请求过于频繁，请稍后重试。")},
    {u"redirect", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 返回了重定向，验证已终止。")},
    {u"request_rejected", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 拒绝了验证请求，请稍后重试。")},
    {u"service_unavailable", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 服务暂时不可用，请稍后重试。")},
    {u"timeout", QT_TRANSLATE_NOOP("ModelConnectionController", "验证 DeepSeek API Key 超时，请稍后重试。")},
    {u"tls", QT_TRANSLATE_NOOP("ModelConnectionController", "无法建立到 DeepSeek 的安全连接，请检查证书和系统时间。")},
    {u"network", QT_TRANSLATE_NOOP("ModelConnectionController", "无法连接到 DeepSeek，请检查网络后重试。")},
    {u"invalid_response", QT_TRANSLATE_NOOP("ModelConnectionController", "DeepSeek 未返回有效的模型列表，请重试。")},
};

QString verificationErrorMessage(QStringView code)
{
    for (const auto &error : VerificationErrors) {
        if (error.code == code)
            return QCoreApplication::translate("ModelConnectionController", error.message);
    }
    return {};
}

QString verificationProfile(const QString &key)
{
    const QByteArray input = (DeepSeekClient::apiEndpoint() + u'\n' + key).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

void restoreSetting(QSettings &settings, QLatin1StringView name, const QVariant &previous)
{
    if (previous.isValid())
        settings.setValue(name, previous);
    else
        settings.remove(name);
}

auto verificationSettings(const QSettings &settings)
{
    return std::array{settings.value(VerificationDigestKey), settings.value(VerificationStatusKey),
                      settings.value(VerificationErrorKey)};
}

void restoreVerificationSettings(QSettings &settings, const std::array<QVariant, VerificationKeys.size()> &previous)
{
    for (std::size_t i = 0; i < VerificationKeys.size(); ++i)
        restoreSetting(settings, VerificationKeys[i], previous[i]);
}

bool persistModelConfiguration(const QString &filePath, const QString &model, const QString &key, bool keyChanged)
{
    if (filePath.isEmpty())
        return false;

    QSettings settings(filePath, QSettings::IniFormat);
    settings.sync();
    if (settings.status() != QSettings::NoError || !settings.isWritable())
        return false;

    const QVariant previousModel = settings.value(AppSettings::ModelIdKey);
    const QVariant previousKey = settings.value(AppSettings::ApiKeyKey);
    const auto previousVerification = verificationSettings(settings);
    const bool invalidateVerification = keyChanged || previousKey.toString().trimmed() != key;
    settings.setValue(AppSettings::ModelIdKey, model);
    if (key.isEmpty())
        settings.remove(AppSettings::ApiKeyKey);
    else
        settings.setValue(AppSettings::ApiKeyKey, key);
    if (invalidateVerification) {
        for (const auto name : VerificationKeys)
            settings.remove(name);
    }
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;

    restoreSetting(settings, AppSettings::ModelIdKey, previousModel);
    restoreSetting(settings, AppSettings::ApiKeyKey, previousKey);
    if (invalidateVerification)
        restoreVerificationSettings(settings, previousVerification);
    settings.sync();
    return false;
}

bool persistVerification(const QString &filePath, const QString &profile, const QString &errorCode)
{
    if (filePath.isEmpty() || profile.isEmpty())
        return false;

    QSettings settings(filePath, QSettings::IniFormat);
    settings.sync();
    const QString storedKey = settings.value(AppSettings::ApiKeyKey).toString().trimmed();
    if (settings.status() != QSettings::NoError || !DeepSeekClient::isValidApiKey(storedKey)
        || verificationProfile(storedKey) != profile || !settings.isWritable())
        return false;

    const auto previous = verificationSettings(settings);
    settings.setValue(VerificationDigestKey, profile);
    settings.setValue(VerificationStatusKey, errorCode.isEmpty() ? QStringLiteral("passed") : QStringLiteral("failed"));
    if (errorCode.isEmpty())
        settings.remove(VerificationErrorKey);
    else
        settings.setValue(VerificationErrorKey, errorCode);
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;

    restoreVerificationSettings(settings, previous);
    settings.sync();
    return false;
}
}

ModelConnectionController::ModelConnectionController(QString configFilePath, QNetworkAccessManager *network,
                                                     QObject *parent)
    : QObject(parent),
      m_configFilePath(std::move(configFilePath)),
      m_client(network ? *network : *new QNetworkAccessManager(this)),
      m_verificationTimeout(this)
{
    QString storedModel;
    if (!m_configFilePath.isEmpty()) {
        const QSettings settings(m_configFilePath, QSettings::IniFormat);
        storedModel = settings.value(AppSettings::ModelIdKey).toString();
        m_savedApiKey = settings.value(AppSettings::ApiKeyKey).toString().trimmed();
        if (modelConfigured()) {
            m_verificationState = VerificationState::Untested;
            restoreVerification(settings);
        }
    }
    m_savedModelName = SupportedModels.contains(storedModel) ? storedModel : SupportedModels.first();
    m_draftModelName = m_savedModelName;
    m_draftApiKey = m_savedApiKey;

    m_verificationTimeout.setSingleShot(true);
    m_verificationTimeout.setTimerType(Qt::PreciseTimer);
    m_verificationTimeout.setInterval(10s);
    connect(&m_verificationTimeout, &QTimer::timeout, this, [this] {
        completeVerification(QStringLiteral("timeout"));
    });
}

ModelConnectionController::~ModelConnectionController()
{
    cancelVerification();
}

QStringList ModelConnectionController::availableModels() const
{
    return SupportedModels;
}

bool ModelConnectionController::modelConfigured() const
{
    return DeepSeekClient::isValidApiKey(m_savedApiKey);
}

bool ModelConnectionController::configurationDirty() const
{
    return m_draftModelName != m_savedModelName || m_draftApiKey.trimmed() != m_savedApiKey;
}

QString ModelConnectionController::verificationStatus() const
{
    switch (m_verificationState) {
    case VerificationState::Unconfigured: return QStringLiteral("unconfigured");
    case VerificationState::Untested: return QStringLiteral("untested");
    case VerificationState::Testing: return QStringLiteral("testing");
    case VerificationState::Passed: return QStringLiteral("passed");
    case VerificationState::Failed: return QStringLiteral("failed");
    }
    return {};
}

void ModelConnectionController::setDraftModelName(const QString &model)
{
    if (m_draftModelName == model)
        return;
    const bool wasDirty = configurationDirty();
    m_draftModelName = model;
    emit draftModelNameChanged();
    if (wasDirty != configurationDirty())
        emit configurationDirtyChanged();
}

void ModelConnectionController::setDraftApiKey(const QString &key)
{
    if (m_draftApiKey == key)
        return;
    const bool wasDirty = configurationDirty();
    m_draftApiKey = key;
    emit draftApiKeyChanged();
    if (wasDirty != configurationDirty())
        emit configurationDirtyChanged();
}

bool ModelConnectionController::testConnection()
{
    if (m_verificationReply)
        return false;
    QNetworkReply *reply = m_client.listModels(m_savedApiKey);
    if (!reply) {
        if (!m_savedApiKey.isEmpty())
            setVerification(VerificationState::Failed, tr("API Key 格式无效，请修改后重试。"));
        return false;
    }

    m_verificationReply = reply;
    m_requestProfile = verificationProfile(m_savedApiKey);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishVerification(reply); });
    m_verificationTimeout.start();
    setVerification(VerificationState::Testing, tr("正在验证 API Key…"));

    if (m_verificationReply == reply && reply->isFinished()) {
        QMetaObject::invokeMethod(this, [this, readyReply = QPointer<QNetworkReply>(reply)] {
            if (readyReply)
                finishVerification(readyReply.data());
        }, Qt::QueuedConnection);
    }
    return true;
}

bool ModelConnectionController::saveModel()
{
    if (!SupportedModels.contains(m_draftModelName)) {
        emit notification(tr("请选择支持的 DeepSeek 模型。"), QStringLiteral("error"));
        return false;
    }

    const QString key = m_draftApiKey.trimmed();
    if (!key.isEmpty() && !DeepSeekClient::isValidApiKey(key)) {
        emit notification(tr("请输入有效的 DeepSeek API Key。"), QStringLiteral("error"));
        return false;
    }
    if (m_draftModelName == m_savedModelName && key == m_savedApiKey)
        return true;

    const bool keyChanged = key != m_savedApiKey;
    if (!persistModelConfiguration(m_configFilePath, m_draftModelName, key, keyChanged)) {
        emit notification(tr("保存模型配置失败，请检查配置目录的访问权限。"), QStringLiteral("error"));
        return false;
    }

    const bool wasConfigured = modelConfigured();
    const bool modelChanged = m_savedModelName != m_draftModelName;
    m_savedApiKey = key;
    m_savedModelName = m_draftModelName;

    if (keyChanged) {
        cancelVerification();
        if (key.isEmpty())
            setVerification(VerificationState::Unconfigured);
        else
            testConnection();
    }

    if (modelChanged)
        emit savedModelNameChanged();
    if (wasConfigured != modelConfigured())
        emit modelConfiguredChanged();
    emit configurationDirtyChanged();
    return true;
}

void ModelConnectionController::cancelVerification()
{
    m_verificationTimeout.stop();
    m_requestProfile.clear();
    if (auto *reply = m_verificationReply.data()) {
        m_verificationReply.clear();
        disconnect(reply, nullptr, this, nullptr);
        if (!reply->isFinished())
            reply->abort();
        reply->deleteLater();
    }
}

void ModelConnectionController::finishVerification(QNetworkReply *reply)
{
    if (m_verificationReply != reply)
        return;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
        completeVerification(verificationFailureCode(*reply, status));
        return;
    }

    const auto models = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("data"));
    if (models.isArray())
        completeVerification();
    else
        completeVerification(QStringLiteral("invalid_response"));
}

void ModelConnectionController::completeVerification(const QString &errorCode)
{
    const QString profile = m_requestProfile;
    cancelVerification();
    const bool stored = persistVerification(m_configFilePath, profile, errorCode);
    if (errorCode.isEmpty())
        setVerification(VerificationState::Passed, tr("API Key 验证通过。"));
    else
        setVerification(VerificationState::Failed, verificationErrorMessage(errorCode));
    if (!stored)
        emit notification(tr("API Key 验证结果未能保存。"), QStringLiteral("error"));
}

void ModelConnectionController::restoreVerification(const QSettings &settings)
{
    const QString profile = settings.value(VerificationDigestKey).toString();
    const QString status = settings.value(VerificationStatusKey).toString();
    const QString error = settings.value(VerificationErrorKey).toString();
    if (settings.status() != QSettings::NoError || profile != verificationProfile(m_savedApiKey))
        return;

    if (status == QStringLiteral("passed") && error.isEmpty()) {
        setVerification(VerificationState::Passed, tr("API Key 验证通过。"), true);
    } else if (status == QStringLiteral("failed")) {
        const QString message = verificationErrorMessage(error);
        if (!message.isEmpty())
            setVerification(VerificationState::Failed, message, true);
    }
}

void ModelConnectionController::setVerification(VerificationState state, const QString &message, bool restored)
{
    if (m_verificationState == state && m_verificationMessage == message && m_verificationRestored == restored)
        return;

    m_verificationState = state;
    m_verificationMessage = message;
    m_verificationRestored = restored;
    emit verificationStatusChanged();
}

QString ModelConnectionController::verificationFailureCode(const QNetworkReply &reply, int status)
{
    switch (status) {
    case 401:
        return QStringLiteral("invalid_key");
    case 403:
        return QStringLiteral("forbidden");
    case 429:
        return QStringLiteral("rate_limited");
    }
    if (status / 100 == 3)
        return QStringLiteral("redirect");
    if (status / 100 == 4)
        return QStringLiteral("request_rejected");
    if (status >= 500)
        return QStringLiteral("service_unavailable");
    if (reply.error() == QNetworkReply::TimeoutError || reply.error() == QNetworkReply::ProxyTimeoutError)
        return QStringLiteral("timeout");
    if (reply.error() == QNetworkReply::SslHandshakeFailedError)
        return QStringLiteral("tls");
    return QStringLiteral("network");
}
