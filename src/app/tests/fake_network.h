#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <cstring>

namespace TestNetwork {

class FakeReply final : public QNetworkReply
{
public:
    explicit FakeReply(QObject *parent)
        : QNetworkReply(parent)
    {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void deliver(int httpStatus, const QByteArray &body)
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        m_body += body;
        emit readyRead();
    }

    void complete(int httpStatus, const QByteArray &body, NetworkError error = NoError)
    {
        if (httpStatus)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        if (error != NoError)
            setError(error, QStringLiteral("synthetic reply details"));
        m_body += body;
        setFinished(true);
        emit readyRead();
        emit finished();
    }

    void abort() override
    {
        complete(0, {}, OperationCanceledError);
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *buffer, qint64 maxSize) override
    {
        if (m_body.isEmpty())
            return isFinished() ? -1 : 0;
        const qint64 length = qMin(maxSize, qint64(m_body.size()));
        std::memcpy(buffer, m_body.constData(), size_t(length));
        m_body.remove(0, length);
        return length;
    }

private:
    QByteArray m_body;
};

class FakeNetwork final : public QNetworkAccessManager
{
public:
    QNetworkRequest request;
    QByteArray body;
    Operation operation = UnknownOperation;
    FakeReply *reply = nullptr;

protected:
    QNetworkReply *createRequest(Operation method, const QNetworkRequest &incomingRequest,
                                 QIODevice *outgoingData) override
    {
        operation = method;
        request = incomingRequest;
        body = outgoingData ? outgoingData->readAll() : QByteArray();
        reply = new FakeReply(this);
        return reply;
    }
};

}
