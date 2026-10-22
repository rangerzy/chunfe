#include "text_block_content.h"

#include <QTextBoundaryFinder>

#include <cmath>
#include <optional>

namespace {

std::optional<qsizetype> offset(const QVariant &value, qsizetype length)
{
    switch (value.metaType().id()) {
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong: {
        const auto number = value.toLongLong();
        if (number >= 0 && number <= length)
            return qsizetype(number);
        break;
    }
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong: {
        const auto number = value.toULongLong();
        if (number <= qulonglong(length))
            return qsizetype(number);
        break;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        const auto number = value.toDouble();
        if (std::isfinite(number) && number >= 0 && std::trunc(number) == number
            && static_cast<long double>(number) <= static_cast<long double>(length))
            return qsizetype(number);
        break;
    }
    default:
        break;
    }
    return std::nullopt;
}

bool validFlag(const QVariantMap &range, const QString &key)
{
    return !range.contains(key) || range.value(key).metaType().id() == QMetaType::Bool;
}

} // namespace

namespace textblock {

Content Content::fromVariant(const QVariantMap &value)
{
    Content result;
    const auto text = value.value(QStringLiteral("text"));
    if (text.metaType().id() != QMetaType::QString)
        return result;
    result.text = text.toString();

    const auto ranges = value.value(QStringLiteral("ranges"));
    if (ranges.metaType().id() != QMetaType::QVariantList)
        return result;

    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, result.text);
    for (const auto &entry : ranges.toList()) {
        if (entry.metaType().id() != QMetaType::QVariantMap)
            continue;
        const auto range = entry.toMap();
        const auto start = offset(range.value(QStringLiteral("start")), result.text.size());
        const auto end = offset(range.value(QStringLiteral("end")), result.text.size());
        if (!start || !end || *start >= *end
            || !validFlag(range, QStringLiteral("bold"))
            || !validFlag(range, QStringLiteral("highlight")))
            continue;
        boundaries.setPosition(*start);
        if (!boundaries.isAtBoundary())
            continue;
        boundaries.setPosition(*end);
        if (!boundaries.isAtBoundary())
            continue;
        result.spans.append({*start, *end, range.value(QStringLiteral("bold")).toBool(),
                            range.value(QStringLiteral("highlight")).toBool()});
    }
    return result;
}

} // namespace textblock
