#pragma once

#include "deepseek_stream.h"

#include <QAbstractItemModel>
#include <QObject>
#include <QString>

#include <optional>

class ModelConnectionController;
class QNetworkAccessManager;

class ModelTestChatController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString modelName READ modelName NOTIFY sessionChanged)
    Q_PROPERTY(bool active READ active NOTIFY sessionChanged)
    Q_PROPERTY(QAbstractItemModel *turns READ turns CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    // The model controller and an injected network manager must outlive this controller.
    explicit ModelTestChatController(ModelConnectionController &model, QNetworkAccessManager *network = nullptr,
                                     QObject *parent = nullptr);

    QString modelName() const;
    bool active() const { return m_session.has_value(); }
    QAbstractItemModel *turns() const;
    bool running() const;

    Q_INVOKABLE bool beginSession();
    Q_INVOKABLE void endSession();
    Q_INVOKABLE bool sendPrompt(const QString &prompt);
    Q_INVOKABLE bool retryLast();

signals:
    void sessionChanged();
    void runningChanged();

private:
    class Turns;
    struct Profile {
        QString modelName;
        QString apiKey;
    };

    bool startReply(const QString &prompt);

    const ModelConnectionController &m_model;
    Turns *const m_turns;
    DeepSeekStream m_stream;
    std::optional<Profile> m_session;
};
