#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVariant>

class ConversationStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList records READ records NOTIFY recordsChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit ConversationStore(QString dataDirectory, QObject *parent = nullptr);
    ~ConversationStore() override;

    QVariantList records() const { return m_records; }
    bool ready() const { return m_ready; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE QString newRecordId() const;
    Q_INVOKABLE QVariantMap record(QString id);
    Q_INVOKABLE bool saveRecord(QVariantMap record);
    Q_INVOKABLE bool renameRecord(QString id, QString title);
    Q_INVOKABLE bool deleteRecord(QString id);

signals:
    void recordsChanged();
    void readyChanged();
    void errorMessageChanged();

private:
    bool initializeSchema();
    bool readSummaries(QVariantList &summaries);
    bool mutate(const QString &statement, const QVariantList &arguments, const QString &error);
    void setErrorMessage(const QString &error);

    QString m_connectionName;
    QSqlDatabase m_database;
    QVariantList m_records;
    QString m_errorMessage;
    bool m_ready = false;
};
