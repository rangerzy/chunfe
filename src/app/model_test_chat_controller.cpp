#include "model_test_chat_controller.h"

#include "model_connection_controller.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>

class ModelTestChatController::Turns final : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;

    enum Role { PromptText = Qt::UserRole + 1, ReplyText, State, Message };
    enum class TurnState { Streaming, Complete, Failed };

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_turns.size());
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{PromptText, "promptText"}, {ReplyText, "replyText"},
                {State, "turnState"}, {Message, "turnMessage"}};
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!hasIndex(index.row(), index.column()))
            return {};

        const Turn &turn = m_turns.at(index.row());
        switch (role) {
        case PromptText: return turn.prompt;
        case ReplyText: return turn.reply;
        case Message: return turn.message;
        case State:
            switch (turn.state) {
            case TurnState::Streaming: return QStringLiteral("streaming");
            case TurnState::Complete: return QStringLiteral("complete");
            case TurnState::Failed: return QStringLiteral("failed");
            }
        }
        return {};
    }

    bool latestIs(TurnState state) const
    {
        return !m_turns.isEmpty() && m_turns.last().state == state;
    }

    QString latestPrompt() const { return m_turns.last().prompt; }

    void appendPrompt(const QString &prompt)
    {
        const int row = rowCount();
        beginInsertRows({}, row, row);
        m_turns.append(Turn{prompt, {}, TurnState::Streaming, {}});
        endInsertRows();
    }

    void appendReply(const QString &text)
    {
        m_turns.last().reply += text;
        const QModelIndex row = index(rowCount() - 1);
        emit dataChanged(row, row, {ReplyText});
    }

    void finishLast(bool success, const QString &message)
    {
        Turn &turn = m_turns.last();
        turn.state = success ? TurnState::Complete : TurnState::Failed;
        turn.message = message;
        const QModelIndex row = index(rowCount() - 1);
        emit dataChanged(row, row, {State, Message});
    }

    void retryLast()
    {
        Turn &turn = m_turns.last();
        turn.reply.clear();
        turn.state = TurnState::Streaming;
        turn.message.clear();
        const QModelIndex row = index(rowCount() - 1);
        emit dataChanged(row, row, {ReplyText, State, Message});
    }

    void clear()
    {
        if (m_turns.isEmpty())
            return;
        beginResetModel();
        m_turns.clear();
        endResetModel();
    }

    QJsonArray requestMessages(const QString &prompt) const
    {
        QJsonArray messages;
        const auto append = [&messages](const QString &role, const QString &content) {
            messages.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}});
        };
        for (const Turn &turn : m_turns) {
            if (turn.state == TurnState::Complete) {
                append(QStringLiteral("user"), turn.prompt);
                append(QStringLiteral("assistant"), turn.reply);
            }
        }
        append(QStringLiteral("user"), prompt);
        return messages;
    }

private:
    struct Turn {
        QString prompt;
        QString reply;
        TurnState state;
        QString message;
    };

    QList<Turn> m_turns;
};

ModelTestChatController::ModelTestChatController(ModelConnectionController &model, QNetworkAccessManager *network,
                                                 QObject *parent)
    : QObject(parent),
      m_model(model),
      m_turns(new Turns(this)),
      m_stream(network ? *network : *new QNetworkAccessManager(this), this)
{
    connect(&m_stream, &DeepSeekStream::textReceived, this, [this](const QString &text) {
        if (running())
            m_turns->appendReply(text);
    });
    connect(&m_stream, &DeepSeekStream::finished, this, [this](bool success, const QString &message) {
        if (!running())
            return;
        m_turns->finishLast(success, message);
        if (!running())
            emit runningChanged();
    });
}

QString ModelTestChatController::modelName() const
{
    return m_session ? m_session->modelName : QString();
}

QAbstractItemModel *ModelTestChatController::turns() const
{
    return m_turns;
}

bool ModelTestChatController::running() const
{
    return active() && m_turns->latestIs(Turns::TurnState::Streaming);
}

bool ModelTestChatController::beginSession()
{
    if (active())
        return true;
    if (!m_model.modelConfigured())
        return false;

    m_session = Profile{m_model.savedModelName(), m_model.savedApiKey()};
    emit sessionChanged();
    return active();
}

void ModelTestChatController::endSession()
{
    if (!active())
        return;

    const bool wasRunning = running();
    m_session.reset();
    m_stream.cancel();
    m_turns->clear();
    if (wasRunning)
        emit runningChanged();
    emit sessionChanged();
}

bool ModelTestChatController::sendPrompt(const QString &prompt)
{
    if (!active() || running() || prompt.trimmed().isEmpty() || !startReply(prompt))
        return false;

    // The stream delivers any immediately available data on the next event-loop turn.
    m_turns->appendPrompt(prompt);
    if (running())
        emit runningChanged();
    return true;
}

bool ModelTestChatController::retryLast()
{
    if (!active() || !m_turns->latestIs(Turns::TurnState::Failed) || !startReply(m_turns->latestPrompt()))
        return false;

    m_turns->retryLast();
    if (running())
        emit runningChanged();
    return true;
}

bool ModelTestChatController::startReply(const QString &prompt)
{
    return m_stream.start(m_session->apiKey, m_session->modelName, m_turns->requestMessages(prompt), {
        {QStringLiteral("max_tokens"), 1024},
        {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}}}
    });
}
