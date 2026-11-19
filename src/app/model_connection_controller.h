#pragma once

#include "deepseek_client.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;

class ModelConnectionController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString apiEndpoint READ apiEndpoint CONSTANT)
    Q_PROPERTY(QStringList availableModels READ availableModels CONSTANT)
    Q_PROPERTY(QString savedModelName READ savedModelName NOTIFY savedModelNameChanged)
    Q_PROPERTY(QString draftModelName READ draftModelName WRITE setDraftModelName NOTIFY draftModelNameChanged)
    Q_PROPERTY(QString draftApiKey READ draftApiKey WRITE setDraftApiKey NOTIFY draftApiKeyChanged)
    Q_PROPERTY(bool modelConfigured READ modelConfigured NOTIFY modelConfiguredChanged)
    Q_PROPERTY(bool configurationDirty READ configurationDirty NOTIFY configurationDirtyChanged)
    Q_PROPERTY(QString verificationStatus READ verificationStatus NOTIFY verificationStatusChanged)
    Q_PROPERTY(QString verificationMessage READ verificationMessage NOTIFY verificationStatusChanged)
    Q_PROPERTY(bool verificationRestored READ verificationRestored NOTIFY verificationStatusChanged)

public:
    // An injected network manager must outlive the controller and share its thread.
    explicit ModelConnectionController(QString configFilePath, QNetworkAccessManager *network = nullptr,
                                       QObject *parent = nullptr);
    ~ModelConnectionController() override;

    QString apiEndpoint() const { return DeepSeekClient::apiEndpoint(); }
    QStringList availableModels() const;
    QString draftModelName() const { return m_draftModelName; }
    QString savedModelName() const { return m_savedModelName; }
    QString savedApiKey() const { return m_savedApiKey; }
    QString draftApiKey() const { return m_draftApiKey; }
    bool modelConfigured() const;
    bool configurationDirty() const;
    QString verificationStatus() const;
    QString verificationMessage() const { return m_verificationMessage; }
    bool verificationRestored() const { return m_verificationRestored; }

    void setDraftModelName(const QString &model);
    void setDraftApiKey(const QString &key);
    Q_INVOKABLE bool testConnection();
    Q_INVOKABLE bool saveModel();

signals:
    void savedModelNameChanged();
    void draftModelNameChanged();
    void draftApiKeyChanged();
    void modelConfiguredChanged();
    void configurationDirtyChanged();
    void verificationStatusChanged();
    void notification(const QString &message, const QString &severity);

private:
    enum class VerificationState { Unconfigured, Untested, Testing, Passed, Failed };

    void cancelVerification();
    void finishVerification(QNetworkReply *reply);
    void completeVerification(const QString &errorCode = {});
    void restoreVerification(const QSettings &settings);
    void setVerification(VerificationState state, const QString &message = {}, bool restored = false);
    static QString verificationFailureCode(const QNetworkReply &reply, int status);

    const QString m_configFilePath;
    DeepSeekClient m_client;
    QPointer<QNetworkReply> m_verificationReply;
    QTimer m_verificationTimeout;
    QString m_savedModelName;
    QString m_draftModelName;
    QString m_savedApiKey;
    QString m_draftApiKey;
    QString m_requestProfile;
    VerificationState m_verificationState = VerificationState::Unconfigured;
    QString m_verificationMessage;
    bool m_verificationRestored = false;
};
