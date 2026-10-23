#pragma once

#include "text_block_content.h"

#include <QColor>
#include <QFont>
#include <QSizeF>
#include <QTextLayout>

namespace textblock {

class ParagraphLayout {
public:
    void rebuild(const Content &content, const QFont &font, qreal maximumWidth,
                 const QColor &highlightColor);
    QSizeF size() const;
    int lineCount() const;
    QTextLayout *textLayout() const;

private:
    mutable QTextLayout m_layout;
    QSizeF m_size{0, 0};
};

} // namespace textblock
