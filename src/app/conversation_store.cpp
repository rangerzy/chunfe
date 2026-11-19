#include "conversation_store.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <utility>

namespace {
QVariantMap recordMetadata(const QSqlQuery &query)
{
    return {
        {QStringLiteral("id"), query.value(0).toString()},
        {QStringLiteral("title"), query.value(1).toString()},
        {QStringLiteral("date"), query.value(2).toString()},
        {QStringLiteral("duration"), query.value(3).toString()},
        {QStringLiteral("model"), query.value(4).toString()},
        {QStringLiteral("isDemo"), false},
    };
}
}

ConversationStore::ConversationStore(QString dataDirectory, QObject *parent)
    : QObject(parent)
{
    const auto openError = tr("无法打开对话记录数据库，请检查本地存储后重试。");
    if (dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory)) {
        setErrorMessage(openError);
        return;
    }

    m_connectionName = QStringLiteral("chunfe-conversations-") + newRecordId();
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(QDir(dataDirectory).absoluteFilePath(QStringLiteral("conversations.sqlite")));
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=2000"));
    if (!m_database.open() || !initializeSchema() || !readSummaries(m_records)) {
        m_records.clear();
        setErrorMessage(openError);
        return;
    }

    m_ready = true;
    emit readyChanged();
}

ConversationStore::~ConversationStore()
{
    m_database.close();
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
}

QString ConversationStore::newRecordId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVariantMap ConversationStore::record(QString id)
{
    if (!m_ready || id.isEmpty())
        return {};

    QSqlQuery query(m_database);
    const auto readError = tr("无法读取对话记录，请稍后重试。");
    if (!query.prepare(QStringLiteral(
            "SELECT id, title, display_date, display_duration, model, started_at, "
            "questions_json, transcript_json, background_text FROM conversations WHERE id = ?"))) {
        setErrorMessage(readError);
        return {};
    }
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        if (query.lastError().isValid())
            setErrorMessage(readError);
        return {};
    }

    QVariantMap result = recordMetadata(query);
    result.insert(QStringLiteral("startedAt"), query.value(5).toLongLong());
    const auto questions = QJsonDocument::fromJson(query.value(6).toString().toUtf8());
    const auto transcript = QJsonDocument::fromJson(query.value(7).toString().toUtf8());
    result.insert(QStringLiteral("backgroundText"), query.value(8).toString());
    query.finish();
    if (!questions.isArray() || !transcript.isArray()) {
        setErrorMessage(readError);
        return {};
    }

    result.insert(QStringLiteral("questions"), questions.toVariant());
    result.insert(QStringLiteral("transcript"), transcript.toVariant());
    setErrorMessage({});
    return result;
}

bool ConversationStore::saveRecord(QVariantMap recordData)
{
    if (!m_ready)
        return false;

    const auto field = [&recordData](const QString &name) {
        const QString value = recordData.value(name).toString();
        return value.isNull() ? QStringLiteral("") : value;
    };
    const QString id = field(QStringLiteral("id"));
    const QString title = field(QStringLiteral("title"));
    const auto questions = QJsonDocument::fromVariant(recordData.value(QStringLiteral("questions")));
    const auto transcript = QJsonDocument::fromVariant(recordData.value(QStringLiteral("transcript")));
    bool hasStartedAt = false;
    const auto startedAt = recordData.value(QStringLiteral("startedAt")).toLongLong(&hasStartedAt);
    if (id.trimmed().isEmpty() || title.trimmed().isEmpty() || !hasStartedAt
        || !questions.isArray() || !transcript.isArray() || recordData.value(QStringLiteral("isDemo")).toBool()) {
        setErrorMessage(tr("对话记录数据无效，无法保存。"));
        return false;
    }

    return mutate(QStringLiteral(
                      "INSERT INTO conversations (id, title, display_date, display_duration, model, started_at, "
                      "question_count, questions_json, transcript_json, background_text) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
                  {id, title, field(QStringLiteral("date")), field(QStringLiteral("duration")),
                   field(QStringLiteral("model")), startedAt, qlonglong(questions.array().size()),
                   QString::fromUtf8(questions.toJson(QJsonDocument::Compact)),
                   QString::fromUtf8(transcript.toJson(QJsonDocument::Compact)),
                   field(QStringLiteral("backgroundText"))},
                  tr("无法保存对话记录，请检查本地存储后重试。"));
}

bool ConversationStore::renameRecord(QString id, QString title)
{
    if (!m_ready)
        return false;
    title = title.trimmed();
    if (title.isEmpty()) {
        setErrorMessage(tr("名称不能为空。"));
        return false;
    }
    return mutate(QStringLiteral("UPDATE conversations SET title = ? WHERE id = ?"), {title, id},
                  tr("无法重命名对话记录，请稍后重试。"));
}

bool ConversationStore::deleteRecord(QString id)
{
    if (!m_ready)
        return false;
    return mutate(QStringLiteral("DELETE FROM conversations WHERE id = ?"), {id},
                  tr("无法删除对话记录，请稍后重试。"));
}

bool ConversationStore::initializeSchema()
{
    if (!m_database.transaction())
        return false;

    bool created = false;
    {
        QSqlQuery query(m_database);
        created = query.exec(QStringLiteral(
                      "CREATE TABLE IF NOT EXISTS conversations ("
                      "id TEXT NOT NULL PRIMARY KEY, title TEXT NOT NULL, "
                      "display_date TEXT NOT NULL, display_duration TEXT NOT NULL, model TEXT NOT NULL, "
                      "started_at INTEGER NOT NULL, question_count INTEGER NOT NULL, "
                      "questions_json TEXT NOT NULL, transcript_json TEXT NOT NULL, "
                      "background_text TEXT NOT NULL)"))
            && query.exec(QStringLiteral(
                "CREATE INDEX IF NOT EXISTS conversations_recent ON conversations (started_at DESC, id DESC)"));
    }
    if (created && m_database.commit())
        return true;
    m_database.rollback();
    return false;
}

bool ConversationStore::readSummaries(QVariantList &summaries)
{
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral(
            "SELECT id, title, display_date, display_duration, model, question_count "
            "FROM conversations ORDER BY started_at DESC, id DESC")))
        return false;

    while (query.next()) {
        auto summary = recordMetadata(query);
        summary.insert(QStringLiteral("questionCount"), query.value(5).toInt());
        summaries.append(summary);
    }
    return !query.lastError().isValid();
}

bool ConversationStore::mutate(const QString &statement, const QVariantList &arguments, const QString &error)
{
    if (!m_database.transaction()) {
        setErrorMessage(error);
        return false;
    }

    bool written = false;
    {
        QSqlQuery query(m_database);
        if (query.prepare(statement)) {
            for (const auto &argument : arguments)
                query.addBindValue(argument);
            written = query.exec() && query.numRowsAffected() == 1;
        }
    }
    QVariantList summaries;
    if (!written || !readSummaries(summaries) || !m_database.commit()) {
        m_database.rollback();
        setErrorMessage(error);
        return false;
    }

    const bool changed = m_records != summaries;
    m_records = std::move(summaries);
    setErrorMessage({});
    if (changed)
        emit recordsChanged();
    return true;
}

void ConversationStore::setErrorMessage(const QString &error)
{
    if (m_errorMessage == error)
        return;
    m_errorMessage = error;
    emit errorMessageChanged();
}
