#pragma once

#include <QString>

class QJsonObject;

namespace ConversationAdviceText {

QString systemPrompt();
bool hasPlaceholders(QStringView promptTemplate);
QString renderPrompt(const QString &promptTemplate, const QString &background, const QJsonObject &context);
bool shouldEvaluate(const QString &text);

class Response final {
public:
    enum class State { Waiting, Ignored, Advice, Invalid };

    void append(const QString &chunk);
    void finish();
    State state() const { return m_state; }
    const QString &kind() const { return m_kind; }
    const QString &question() const { return m_question; }
    const QString &answer() const { return m_answer; }

private:
    void appendText(QStringView text);
    void acceptHeader();
    void invalidate();

    State m_state = State::Waiting;
    QString m_header;
    QString m_kind;
    QString m_question;
    QString m_bodyPrefix;
    QString m_answer;
    QChar m_pendingHighSurrogate;
    bool m_headerComplete = false;
    bool m_afterCarriageReturn = false;
    bool m_finished = false;
};

}
