#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class BackgroundController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString backgroundText READ backgroundText NOTIFY backgroundTextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit BackgroundController(QString dataDirectory, QObject *parent = nullptr);

    QString backgroundText() const { return m_backgroundText; }
    QString errorMessage() const { return m_errorMessage; }
    Q_INVOKABLE bool importMarkdown(const QUrl &source);

signals:
    void backgroundTextChanged();
    void errorMessageChanged();

private:
    static QString readMarkdown(const QString &path, QByteArray &bytes, QString &text);
    bool rejectImport(const QString &error);

    const QString m_managedFilePath;
    QString m_backgroundText;
    QString m_errorMessage;
};
