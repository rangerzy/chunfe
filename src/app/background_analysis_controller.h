#pragma once

#include "deepseek_stream.h"

#include <QObject>
#include <QString>

class BackgroundController;
class ModelConnectionController;
class QNetworkAccessManager;

class BackgroundAnalysisController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY configuredChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultTextChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY statusChanged)

public:
    // The document, model, and any injected network manager outlive this controller.
    explicit BackgroundAnalysisController(const QString &configFilePath, BackgroundController &document,
                                          ModelConnectionController &model, QNetworkAccessManager *network = nullptr,
                                          QObject *parent = nullptr);

    bool configured() const;
    bool running() const { return m_running; }
    QString resultText() const { return m_resultText; }
    QString statusMessage() const { return m_statusMessage; }
    bool failed() const { return m_failed; }

    Q_INVOKABLE bool startAnalysis();
    Q_INVOKABLE void cancelAnalysis();

signals:
    void configuredChanged();
    void statusChanged();
    void resultTextChanged();

private:
    void setStatus(bool running, bool failed, const QString &message = {});
    void resetAnalysis(bool running);

    const QString m_configFilePath;
    BackgroundController &m_document;
    ModelConnectionController &m_model;
    DeepSeekStream m_stream;
    QString m_resultText;
    QString m_statusMessage;
    bool m_running = false;
    bool m_failed = false;
};
