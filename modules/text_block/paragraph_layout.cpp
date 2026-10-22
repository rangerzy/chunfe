#include "paragraph_layout.h"

#include <QGlyphRun>
#include <QTextCharFormat>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace {

struct FormatBoundary {
    qsizetype position;
    int boldChange;
    int highlightChange;
};

QList<QTextLayout::FormatRange> formats(const textblock::Content &content,
                                      const QColor &highlightColor)
{
    QVector<FormatBoundary> boundaries;
    boundaries.reserve(content.spans.size() * 2);
    for (const auto &span : content.spans) {
        if (span.start < 0 || span.start >= span.end || span.end > content.text.size())
            continue;
        boundaries.append({span.start, int(span.bold), int(span.highlight)});
        boundaries.append({span.end, -int(span.bold), -int(span.highlight)});
    }
    std::sort(boundaries.begin(), boundaries.end(), [](const auto &left, const auto &right) {
        return left.position < right.position;
    });

    QList<QTextLayout::FormatRange> result;
    qsizetype previous = 0;
    int bold = 0;
    int highlighted = 0;
    for (const auto &boundary : boundaries) {
        if (boundary.position > previous && (bold > 0 || highlighted > 0)) {
            QTextCharFormat format;
            if (bold > 0)
                format.setFontWeight(QFont::Bold);
            if (highlighted > 0)
                format.setBackground(highlightColor);
            result.append({int(previous), int(boundary.position - previous), format});
        }
        bold += boundary.boldChange;
        highlighted += boundary.highlightChange;
        previous = boundary.position;
    }
    return result;
}

QString displayText(QString text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\r')) {
            // A CRLF remains two UTF-16 units so every style offset remains valid.
            // WORD JOINER is invisible; the following LINE SEPARATOR makes one break.
            text[i] = i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')
                ? QChar(0x2060) : QChar::LineSeparator;
        } else if (text.at(i) == QLatin1Char('\n')) {
            text[i] = QChar::LineSeparator;
        }
    }
    return text;
}

QRectF inkBounds(const QList<QGlyphRun> &runs)
{
    QRectF result;
    for (const auto &run : runs) {
        const auto glyphs = run.glyphIndexes();
        const auto positions = run.positions();
        const auto font = run.rawFont();
        for (qsizetype i = 0; i < glyphs.size(); ++i) {
            const auto bounds = font.boundingRect(glyphs.at(i)).translated(positions.at(i));
            if (!bounds.isEmpty() && std::isfinite(bounds.left()) && std::isfinite(bounds.top())
                && std::isfinite(bounds.right()) && std::isfinite(bounds.bottom()))
                result |= bounds;
        }
    }
    return result;
}

bool hasMultipleGraphemes(const QTextLayout &layout, const QTextLine &line)
{
    const auto text = layout.text();
    int start = line.textStart();
    int end = start + line.textLength();
    while (start < end && text.at(start).isSpace())
        ++start;
    while (end > start && text.at(end - 1).isSpace())
        --end;
    return start < end && layout.nextCursorPosition(start) < end;
}

} // namespace

namespace textblock {

void ParagraphLayout::rebuild(const Content &content, const QFont &font, qreal maximumWidth,
                              const QColor &highlightColor)
{
    m_size = QSizeF(0, 0);
    m_layout.clearLayout();
    m_layout.setText(displayText(content.text));
    m_layout.setFont(font);
    m_layout.setCacheEnabled(true);
    m_layout.setFormats(formats(content, highlightColor));
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft);
    // Match Qt Quick Text's layout metrics for its QtRendering path.
    option.setUseDesignMetrics(true);
    m_layout.setTextOption(option);
    if (content.text.isEmpty())
        return;

    const qreal width = std::isfinite(maximumWidth) ? std::max(qreal(0), maximumWidth) : 0;
    // Qt's shaping engine uses fixed-point widths internally. Keep extreme input
    // finite and allow a whole grapheme to exceed a very narrow clipping viewport.
    constexpr qreal minimumLineWidth = 0.01;
    const qreal boundedWidth = std::min(width, qreal(1e7));
    qreal height = 0;
    qreal naturalWidth = 0;
    qreal lineWidth = std::max(minimumLineWidth, boundedWidth);
    qreal leftInset = 0;
    qreal rightInset = 0;
    QRectF ink;
    while (true) {
        height = 0;
        naturalWidth = 0;
        m_layout.beginLayout();
        while (true) {
            auto line = m_layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(lineWidth);
            line.setPosition(QPointF(0, height));
            height += line.height();
            naturalWidth = std::max(naturalWidth, line.naturalTextWidth());
        }
        m_layout.endLayout();

        ink = QRectF();
        QVector<qreal> lineRightEdges;
        lineRightEdges.reserve(m_layout.lineCount());
        for (int i = 0; i < m_layout.lineCount(); ++i) {
            const auto line = m_layout.lineAt(i);
            const auto lineInk = inkBounds(line.glyphRuns(-1, -1, QTextLayout::RetrieveGlyphIndexes
                                                                 | QTextLayout::RetrieveGlyphPositions));
            ink |= lineInk;
            lineRightEdges.append(std::max(line.naturalTextWidth(), lineInk.right()));
        }
        leftInset = std::max(qreal(0), -ink.left());
        rightInset = std::max(qreal(0), ink.right() - naturalWidth);
        if (naturalWidth + leftInset + rightInset <= width || lineWidth <= minimumLineWidth)
            break;

        // Wrapping uses advances, which can be narrower than italic ink and even
        // naturalTextWidth(). Reduce by the actual overflow, including when the
        // bearing is unchanged. A single oversized grapheme must not force every
        // other line into a narrower column.
        qreal overflow = 0;
        qreal nextLineWidth = lineWidth;
        constexpr qreal shapingUnit = 1.0 / 64.0;
        for (int i = 0; i < m_layout.lineCount(); ++i) {
            const auto line = m_layout.lineAt(i);
            const qreal lineOverflow = lineRightEdges.at(i) + leftInset - width;
            if (lineOverflow > 0 && hasMultipleGraphemes(m_layout, line)) {
                overflow = std::max(overflow, lineOverflow);
                nextLineWidth = std::min(nextLineWidth, line.horizontalAdvance() - shapingUnit);
            }
        }
        if (overflow <= 0)
            break;
        lineWidth = std::max(minimumLineWidth,
                            std::min(nextLineWidth, lineWidth - std::max(overflow, shapingUnit)));
    }

    const qreal topInset = std::max(qreal(0), -ink.top());
    for (int i = 0; i < m_layout.lineCount(); ++i) {
        auto line = m_layout.lineAt(i);
        line.setPosition(line.position() + QPointF(leftInset, topInset));
    }
    naturalWidth += leftInset + rightInset;
    height = std::max(height, ink.bottom()) + topInset;
    m_size = QSizeF(std::isfinite(naturalWidth) ? std::clamp(naturalWidth, qreal(0), width) : 0,
                   std::isfinite(height) ? std::max(qreal(0), height) : 0);
}

QSizeF ParagraphLayout::size() const { return m_size; }
int ParagraphLayout::lineCount() const { return m_layout.lineCount(); }
QTextLayout *ParagraphLayout::textLayout() const { return &m_layout; }

} // namespace textblock
