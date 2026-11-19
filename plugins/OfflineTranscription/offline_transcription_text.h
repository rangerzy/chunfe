#pragma once

#include <QList>
#include <QStringView>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

struct OfflineTranscriptMatch {
    qsizetype previousStart;
    qsizetype previousEnd;
    qsizetype currentStart;
    qsizetype currentEnd;
    qsizetype previousIndex;
    qsizetype currentIndex;
};

struct OfflineTranscriptAlignment {
    QList<OfflineTranscriptMatch> matches;
    qsizetype previousUnits = 0;
    qsizetype currentUnits = 0;
};

namespace OfflineTranscriptionDetail {
struct LexicalUnit {
    qsizetype start;
    qsizetype end;
};

inline char32_t codePoint(QStringView text, qsizetype offset)
{
    const QChar first = text[offset];
    if (first.isHighSurrogate() && offset + 1 < text.size() && text[offset + 1].isLowSurrogate())
        return QChar::surrogateToUcs4(first, text[offset + 1]);
    return first.unicode();
}

inline bool usesCharacterUnits(char32_t point)
{
    switch (QChar::script(point)) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Thai:
    case QChar::Script_Lao:
    case QChar::Script_Khmer:
        return true;
    default:
        return false;
    }
}

inline QList<LexicalUnit> lexicalUnits(QStringView text)
{
    // Decode output is token-capped, but keep the alignment matrix bounded
    // independently of the input text.
    constexpr qsizetype MaxUnits = 256;
    QList<LexicalUnit> units;
    for (qsizetype offset = 0; offset < text.size() && units.size() < MaxUnits;) {
        const qsizetype start = offset;
        const auto point = codePoint(text, offset);
        offset += QChar::requiresSurrogates(point) ? 2 : 1;
        if (!QChar::isLetterOrNumber(point)) continue;

        const bool characterUnit = usesCharacterUnits(point);
        while (offset < text.size()) {
            const auto next = codePoint(text, offset);
            const bool wordContinuation = !characterUnit && QChar::isLetterOrNumber(next) && !usesCharacterUnits(next);
            if (!wordContinuation && !QChar::isMark(next)) break;
            offset += QChar::requiresSurrogates(next) ? 2 : 1;
        }
        units.append({start, offset});
    }
    return units;
}

inline bool isAbbreviationPeriod(QStringView text, qsizetype period)
{
    qsizetype start = period;
    while (start > 0) {
        const auto ch = text[start - 1].toLower().unicode();
        if (ch != '.' && (ch < 'a' || ch > 'z')) break;
        --start;
    }
    const auto word = text.sliced(start, period - start);
    constexpr std::array<QStringView, 12> Abbreviations = {
        u"mr", u"mrs", u"ms", u"dr", u"prof", u"sr", u"jr", u"st", u"vs", u"etc", u"e.g", u"i.e"
    };
    for (const auto abbreviation : Abbreviations)
        if (word.compare(abbreviation, Qt::CaseInsensitive) == 0) return true;

    // Initials and initialisms, including the first period in a form such as U.S.
    if (word.isEmpty() || word.size() % 2 == 0) return false;
    for (qsizetype i = 1; i < word.size(); i += 2)
        if (word[i] != QLatin1Char('.')) return false;
    for (qsizetype i = 0; i < word.size(); i += 2)
        if (!word[i].isLetter()) return false;
    return true;
}
}

inline OfflineTranscriptAlignment alignOfflineTranscript(QStringView previous, QStringView current,
                                                         double previousWindowShift)
{
    using OfflineTranscriptionDetail::lexicalUnits;
    const auto oldUnits = lexicalUnits(previous);
    const auto newUnits = lexicalUnits(current);
    OfflineTranscriptAlignment alignment;
    alignment.previousUnits = oldUnits.size();
    alignment.currentUnits = newUnits.size();
    if (oldUnits.isEmpty() || newUnits.isEmpty()) return alignment;

    // The PCM shift disambiguates repeated words. Match whole lexical units
    // so revisions in punctuation and spacing don't break audio overlap.
    constexpr int ConsecutiveBonus = 24;
    const double expectedShift = previousWindowShift * double(oldUnits.size());
    enum class Step : unsigned char { SkipPrevious, SkipCurrent, Match };
    struct Cell {
        int score = 0;
        int matchScore = 0;
        Step step = Step::SkipPrevious;
        bool consecutive = false;
    };
    const qsizetype columns = newUnits.size() + 1;
    std::vector<Cell> matrix(size_t((oldUnits.size() + 1) * columns));
    auto cellAt = [&](qsizetype i, qsizetype j) -> Cell & { return matrix[size_t(i * columns + j)]; };

    for (qsizetype i = 1; i <= oldUnits.size(); ++i) {
        const auto &oldUnit = oldUnits[i - 1];
        const auto oldWord = previous.sliced(oldUnit.start, oldUnit.end - oldUnit.start);
        for (qsizetype j = 1; j <= newUnits.size(); ++j) {
            auto &cell = cellAt(i, j);
            const auto &up = cellAt(i - 1, j);
            const auto &left = cellAt(i, j - 1);
            if (left.score > up.score) {
                cell.score = left.score;
                cell.step = Step::SkipCurrent;
            } else {
                cell.score = up.score;
            }

            const auto &newUnit = newUnits[j - 1];
            const auto newWord = current.sliced(newUnit.start, newUnit.end - newUnit.start);
            if (oldWord.compare(newWord, Qt::CaseInsensitive) != 0) continue;

            const double distance = std::abs(double(i - j) - expectedShift);
            const int reward = 100 - int(std::min(95.0, distance * 35.0));
            const auto &diagonal = cellAt(i - 1, j - 1);
            cell.consecutive = diagonal.matchScore > 0 && diagonal.matchScore + ConsecutiveBonus >= diagonal.score;
            cell.matchScore = reward + (cell.consecutive ? diagonal.matchScore + ConsecutiveBonus : diagonal.score);
            if (cell.matchScore >= cell.score) {
                cell.score = cell.matchScore;
                cell.step = Step::Match;
            }
        }
    }

    bool extendRun = false;
    for (qsizetype i = oldUnits.size(), j = newUnits.size(); i > 0 && j > 0;) {
        const auto &cell = cellAt(i, j);
        const auto step = extendRun ? Step::Match : cell.step;
        if (step == Step::Match) {
            const auto &oldUnit = oldUnits[--i];
            const auto &newUnit = newUnits[--j];
            alignment.matches.append({oldUnit.start, oldUnit.end, newUnit.start, newUnit.end, i, j});
            extendRun = cell.consecutive;
        } else if (step == Step::SkipPrevious) {
            --i;
        } else {
            --j;
        }
    }
    std::reverse(alignment.matches.begin(), alignment.matches.end());
    return alignment;
}

inline QList<qsizetype> offlineSentenceEnds(QStringView text)
{
    constexpr QStringView Terminal = u"。！？.!?";
    constexpr QStringView Closing = u"”’」』）》】〕〉）)]}";
    QList<qsizetype> ends;
    bool doubleQuoteOpen = false;
    bool singleQuoteOpen = false;
    for (qsizetype pos = 0; pos < text.size(); ++pos) {
        const auto ch = text[pos];
        if (ch == QLatin1Char('"')) doubleQuoteOpen = !doubleQuoteOpen;
        if (ch == QLatin1Char('\'') &&
            !(pos > 0 && pos + 1 < text.size() && text[pos - 1].isLetterOrNumber() && text[pos + 1].isLetterOrNumber()))
            singleQuoteOpen = !singleQuoteOpen;
        if (!Terminal.contains(ch)) continue;
        if (ch == QLatin1Char('.')) {
            if (OfflineTranscriptionDetail::isAbbreviationPeriod(text, pos)) continue;
            if (pos > 0 && pos + 1 < text.size() && text[pos - 1].isDigit() && text[pos + 1].isDigit()) continue;
        }

        qsizetype end = pos + 1;
        while (end < text.size()) {
            const auto mark = text[end];
            if (mark == QLatin1Char('"') || mark == QLatin1Char('\'')) {
                bool &quoteOpen = mark == QLatin1Char('"') ? doubleQuoteOpen : singleQuoteOpen;
                if (!quoteOpen && end + 1 < text.size() && text[end + 1].isLetterOrNumber()) break;
                quoteOpen = false;
            } else if (!Terminal.contains(mark) && !Closing.contains(mark)) {
                break;
            }
            ++end;
        }
        ends.append(end);
        pos = end - 1;
    }
    return ends;
}
