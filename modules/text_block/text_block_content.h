#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>

namespace textblock {

struct Span {
    qsizetype start = 0;
    qsizetype end = 0;
    bool bold = false;
    bool highlight = false;
    bool operator==(const Span &) const = default;
};

struct Content {
    QString text;
    QVector<Span> spans;
    static Content fromVariant(const QVariantMap &value);
    bool operator==(const Content &) const = default;
};

} // namespace textblock
