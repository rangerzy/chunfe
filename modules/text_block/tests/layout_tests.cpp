#include "paragraph_layout.h"
#include "text_block_content.h"

#include <QGuiApplication>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QTextCharFormat>
#include <QtTest>

#include <cmath>
#include <limits>

namespace {

QVariantMap range(QVariant start, QVariant end, bool bold = false, bool highlight = false)
{
    return {{QStringLiteral("start"), start}, {QStringLiteral("end"), end},
            {QStringLiteral("bold"), bold}, {QStringLiteral("highlight"), highlight}};
}

QTextCharFormat formatAt(const QTextLayout &layout, int position)
{
    QTextCharFormat result;
    for (const auto &range : layout.formats()) {
        if (position >= range.start && position < range.start + range.length)
            result.merge(range.format);
    }
    return result;
}

QFont testFont()
{
    QFont result;
    const auto families = QFontDatabase::families();
    if (families.contains(QStringLiteral("Helvetica Neue")))
        result.setFamily(QStringLiteral("Helvetica Neue"));
    else if (!families.isEmpty())
        result.setFamily(families.first());
    result.setPixelSize(20);
    return result;
}

QRectF renderedInk(const QTextLayout &layout)
{
    QRectF result;
    for (const auto &run : layout.glyphRuns()) {
        const auto glyphs = run.glyphIndexes();
        const auto positions = run.positions();
        for (qsizetype i = 0; i < glyphs.size(); ++i) {
            const auto glyphBounds = run.rawFont().boundingRect(glyphs.at(i));
            if (!glyphBounds.isEmpty())
                result |= glyphBounds.translated(positions.at(i));
        }
    }
    return result;
}

} // namespace

class LayoutTests : public QObject
{
    Q_OBJECT

private slots:
    void preservesPlainTextAndNormalizesRanges();
    void ignoresMalformedRanges();
    void acceptsOnlyWholeGraphemeRanges();
    void combinesOverlappingStyles();
    void reflowsAndHugsShortContent();
    void preservesExplicitLineBreakOffsets();
    void emptyAndInvalidWidthsStayFinite();
    void styleOnlyRebuildReplacesOldFormats();
    void includesItalicInkInsideMeasuredBounds();
    void wrappedItalicFitsWhenGraphemesFit();
    void oversizedGraphemeDoesNotNarrowFollowingLines();
};

void LayoutTests::preservesPlainTextAndNormalizesRanges()
{
    const auto result = textblock::Content::fromVariant({
        {QStringLiteral("text"), QStringLiteral("<b>plain & text</b>")},
        {QStringLiteral("ranges"), QVariantList{range(3.0, 8.0, true), range(11, 15, false, true)}}});
    QCOMPARE(result.text, QStringLiteral("<b>plain & text</b>"));
    QCOMPARE(result.spans.size(), 2);
    QCOMPARE(result.spans.at(0), (textblock::Span{3, 8, true, false}));
    QCOMPARE(result.spans.at(1), (textblock::Span{11, 15, false, true}));
    QVERIFY(textblock::Content::fromVariant({{QStringLiteral("text"), 123}}).text.isEmpty());
}

void LayoutTests::ignoresMalformedRanges()
{
    QVariantList invalid{
        range(-1, 2, true), range(0, 7, true), range(2, 2, true), range(4, 2, true),
        range(0.5, 2, true), range(0, 2.5, true), range(true, 2, true),
        range(QStringLiteral("0"), 2, true), range(0, QStringLiteral("2"), true),
        range(std::numeric_limits<double>::quiet_NaN(), 2, true),
        range(0, std::numeric_limits<double>::infinity(), true),
        range(0, std::numeric_limits<qulonglong>::max(), true),
        range(std::numeric_limits<qlonglong>::min(), 2, true),
        range(QVariant(), 2, true), QVariantMap{{QStringLiteral("end"), 2}},
        QStringLiteral("not a range"),
        QVariantMap{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 2},
                    {QStringLiteral("bold"), QStringLiteral("true")}}
    };
    invalid.append(range(1, 3, true));
    const auto result = textblock::Content::fromVariant({
        {QStringLiteral("text"), QStringLiteral("abcdef")}, {QStringLiteral("ranges"), invalid}});
    QCOMPARE(result.spans, (QVector<textblock::Span>{{1, 3, true, false}}));
}

void LayoutTests::acceptsOnlyWholeGraphemeRanges()
{
    const QString text = QString::fromUtf8("A👩‍💻é🇨🇳Z");
    QCOMPARE(text.size(), 13);
    const auto result = textblock::Content::fromVariant({
        {QStringLiteral("text"), text},
        {QStringLiteral("ranges"), QVariantList{
             range(1, 2, true), range(1, 3, true), range(4, 6, true),
             range(6, 7, true), range(8, 10, true),
             range(1, 6, true), range(6, 8, false, true), range(8, 12, true, true)}}});
    QCOMPARE(result.spans, (QVector<textblock::Span>{
             {1, 6, true, false}, {6, 8, false, true}, {8, 12, true, true}}));
    QCOMPARE(result.text, text);
}

void LayoutTests::combinesOverlappingStyles()
{
    textblock::ParagraphLayout layout;
    layout.rebuild({QStringLiteral("abcdef"), {{0, 4, true, false}, {2, 6, false, true}}},
                   testFont(), 500, QColor(Qt::yellow));
    QVERIFY(layout.textLayout());
    const auto first = formatAt(*layout.textLayout(), 1);
    const auto overlap = formatAt(*layout.textLayout(), 3);
    const auto last = formatAt(*layout.textLayout(), 5);
    QCOMPARE(first.fontWeight(), int(QFont::Bold));
    QCOMPARE(overlap.fontWeight(), int(QFont::Bold));
    QCOMPARE(overlap.background().color(), QColor(Qt::yellow));
    QCOMPARE(last.background().color(), QColor(Qt::yellow));
    QVERIFY(last.fontWeight() != QFont::Bold);
    QCOMPARE(layout.textLayout()->textOption().alignment(), Qt::AlignLeft);
    QVERIFY(layout.textLayout()->textOption().useDesignMetrics());
}

void LayoutTests::reflowsAndHugsShortContent()
{
    textblock::ParagraphLayout layout;
    const textblock::Content content{QStringLiteral("A paragraph with several words and wrapping."), {}};
    layout.rebuild(content, testFont(), 1000, Qt::yellow);
    QCOMPARE(layout.lineCount(), 1);
    const auto wideSize = layout.size();
    QVERIFY(wideSize.width() > 0);
    QVERIFY(wideSize.width() < 1000);
    layout.rebuild(content, testFont(), 90, Qt::yellow);
    QVERIFY(layout.lineCount() > 1);
    QVERIFY(layout.size().height() > wideSize.height());
    QVERIFY(layout.size().width() <= 90);
    const auto *textLayout = layout.textLayout();
    for (int i = 1; i < textLayout->lineCount(); ++i) {
        QVERIFY(textLayout->lineAt(i).y() >= textLayout->lineAt(i - 1).y()
                + textLayout->lineAt(i - 1).height());
    }
    layout.rebuild(content, testFont(), 1000, Qt::yellow);
    QCOMPARE(layout.lineCount(), 1);
    QCOMPARE(layout.size(), wideSize);
}

void LayoutTests::preservesExplicitLineBreakOffsets()
{
    textblock::ParagraphLayout layout;
    const textblock::Content content{QStringLiteral("A\r\nB\rC\nD"), {{3, 4, true, false}}};
    layout.rebuild(content, testFont(), 1000, Qt::yellow);
    QCOMPARE(layout.lineCount(), 4);
    QCOMPARE(layout.textLayout()->text().size(), content.text.size());
    QCOMPARE(layout.textLayout()->lineAt(1).textStart(), 3);
    QCOMPARE(layout.textLayout()->lineAt(2).textStart(), 5);
    QCOMPARE(layout.textLayout()->lineAt(3).textStart(), 7);
    QCOMPARE(formatAt(*layout.textLayout(), 3).fontWeight(), int(QFont::Bold));
    QCOMPARE(content.text, QStringLiteral("A\r\nB\rC\nD"));
    layout.rebuild({QStringLiteral("one\ntwo\n"), {}}, testFont(), 1000, Qt::yellow);
    QCOMPARE(layout.lineCount(), 3);
}

void LayoutTests::emptyAndInvalidWidthsStayFinite()
{
    textblock::ParagraphLayout layout;
    layout.rebuild({}, testFont(), 300, Qt::yellow);
    QCOMPARE(layout.size(), QSizeF(0, 0));
    QCOMPARE(layout.lineCount(), 0);
    const QList<qreal> widths{0, -5, std::numeric_limits<qreal>::quiet_NaN(),
                              std::numeric_limits<qreal>::infinity(), 0.01, 1};
    for (qreal width : widths) {
        layout.rebuild({QString::fromUtf8("你好👩‍💻abc"), {}}, testFont(), width, Qt::yellow);
        QVERIFY(std::isfinite(layout.size().width()));
        QVERIFY(std::isfinite(layout.size().height()));
        QVERIFY(layout.size().width() >= 0);
        QVERIFY(layout.size().height() > 0);
        QVERIFY(layout.lineCount() > 0);
        if (std::isfinite(width) && width >= 0)
            QVERIFY(layout.size().width() <= width);
    }
    layout.rebuild({}, testFont(), 0, Qt::yellow);
    QCOMPARE(layout.size(), QSizeF(0, 0));
    QCOMPARE(layout.lineCount(), 0);
}

void LayoutTests::styleOnlyRebuildReplacesOldFormats()
{
    textblock::ParagraphLayout layout;
    layout.rebuild({QStringLiteral("same text"), {{0, 4, true, true}}}, testFont(), 500, Qt::yellow);
    QVERIFY(!layout.textLayout()->formats().isEmpty());
    layout.rebuild({QStringLiteral("same text"), {{5, 9, false, true}}}, testFont(), 500, Qt::cyan);
    QVERIFY(formatAt(*layout.textLayout(), 0).fontWeight() != QFont::Bold);
    QCOMPARE(formatAt(*layout.textLayout(), 6).background().color(), QColor(Qt::cyan));
    layout.rebuild({QStringLiteral("same text"), {}}, testFont(), 500, Qt::yellow);
    QVERIFY(layout.textLayout()->formats().isEmpty());
}

void LayoutTests::includesItalicInkInsideMeasuredBounds()
{
    auto font = testFont();
    font.setPixelSize(48);
    font.setItalic(true);
    textblock::ParagraphLayout layout;
    const QStringList texts{QStringLiteral("j"), QStringLiteral("Aj"),
                            QStringLiteral("j j j j j j j"), QString::fromUtf8("j אבג é")};
    for (const auto &text : texts) {
        for (qreal maximumWidth : {qreal(500), qreal(75)}) {
            layout.rebuild({text, {}}, font, maximumWidth, Qt::yellow);
            const auto ink = renderedInk(*layout.textLayout());
            QVERIFY2(ink.left() >= -0.01, qPrintable(text));
            QVERIFY2(ink.top() >= -0.01, qPrintable(text));
            QVERIFY2(ink.right() <= layout.size().width() + 0.01, qPrintable(text));
            QVERIFY2(ink.bottom() <= layout.size().height() + 0.01, qPrintable(text));
            QVERIFY(layout.size().width() <= maximumWidth);
        }
    }
}

void LayoutTests::wrappedItalicFitsWhenGraphemesFit()
{
    QStringList families{testFont().family()};
    if (QFontDatabase::families().contains(QStringLiteral("PingFang SC")))
        families.append(QStringLiteral("PingFang SC"));
    textblock::ParagraphLayout layout;
    for (const auto &family : families) {
        for (int pixelSize : {18, 32, 48}) {
            QFont font(family);
            font.setPixelSize(pixelSize);
            font.setItalic(true);
            for (qreal width : {pixelSize * 0.7, pixelSize * 1.5625, qreal(240)}) {
                layout.rebuild({QStringLiteral("j j j j j j j j j"), {}}, font, width, Qt::yellow);
                const auto ink = renderedInk(*layout.textLayout());
                const auto context = QStringLiteral("%1, %2 px, width %3, ink right %4, measured %5")
                    .arg(family).arg(pixelSize).arg(width).arg(ink.right()).arg(layout.size().width());
                QVERIFY2(ink.left() >= -0.01, qPrintable(context));
                QVERIFY2(ink.right() <= layout.size().width() + 0.01, qPrintable(context));
                QVERIFY(layout.size().width() <= width);
            }
        }
    }
}

void LayoutTests::oversizedGraphemeDoesNotNarrowFollowingLines()
{
    auto font = testFont();
    font.setPixelSize(48);
    textblock::ParagraphLayout layout;
    layout.rebuild({QString::fromUtf8("👩‍💻\niii iii"), {}}, font, 35, Qt::yellow);
    QVERIFY(layout.lineCount() >= 2);
    QCOMPARE(layout.textLayout()->lineAt(1).textStart(), 6);
    QVERIFY(layout.textLayout()->lineAt(1).textLength() >= 3);
}

QTEST_MAIN(LayoutTests)
#include "layout_tests.moc"
