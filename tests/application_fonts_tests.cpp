#include "app_fonts.h"
#include "paragraph_layout.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QTest>
#include <memory>

Q_IMPORT_QML_PLUGIN(TextBlockPlugin)

class ApplicationFontsTests : public QObject
{
    Q_OBJECT

    void verifyGlyphs(const QTextLayout &layout, const QString &token,
                      const QString &family, int weight)
    {
        const int start = layout.text().indexOf(token);
        QVERIFY(start >= 0);
        const auto runs = layout.glyphRuns(start, token.size());
        QVERIFY(!runs.isEmpty());
        for (const auto &run : runs) {
            QCOMPARE(run.rawFont().familyName(), family);
            QCOMPARE(run.rawFont().weight(), weight);
            QVERIFY(!run.glyphIndexes().contains(0));
        }
    }

private slots:
    void bundledDefaults()
    {
        const QFont font = QGuiApplication::font();
        QCOMPARE(font.families(), QStringList({"JetBrains Mono", "OPPO Sans 4.0"}));
        QCOMPARE(font.weight(), QFont::Normal);
        QVERIFY(font.styleName().isEmpty());
        QVERIFY(QFontDatabase::families().contains("JetBrains Mono"));
        QVERIFY(QFontDatabase::families().contains("OPPO Sans 4.0"));
        QVERIFY(ChunfeFonts::initialize());
        QCOMPARE(QGuiApplication::font(), font);
        const QString notices = ChunfeFonts::licenseNotices();
        QVERIFY(notices.contains("OPPO Sans Fonts License Agreement"));
        QVERIFY(notices.contains("SIL OPEN FONT LICENSE Version 1.1"));
    }

    void mixedTextUsesRequestedFamilies_data()
    {
        QTest::addColumn<int>("weight");
        QTest::newRow("body") << int(QFont::Normal);
        QTest::newRow("heading") << int(QFont::DemiBold);
        QTest::newRow("bold") << int(QFont::Bold);
    }

    void mixedTextUsesRequestedFamilies()
    {
        QFETCH(int, weight);
        QFont font = QGuiApplication::font();
        font.setPixelSize(18);
        font.setWeight(QFont::Weight(weight));
        textblock::ParagraphLayout paragraph;
        paragraph.rebuild({QStringLiteral("中文，。！？ ABC xyz 0123456789 ,.!?"), {}},
                          font, 700, Qt::yellow);
        const QTextLayout &layout = *paragraph.textLayout();
        verifyGlyphs(layout, QStringLiteral("中文，。！？"), "OPPO Sans 4.0", weight);
        verifyGlyphs(layout, QStringLiteral("ABC xyz 0123456789 ,.!?"), "JetBrains Mono", weight);
    }

    void emphasisKeepsBothFamilies()
    {
        const QString text = QStringLiteral("正文 ABC 123；重点 XYZ 456。");
        const int start = text.indexOf(QStringLiteral("重点"));
        QFont font = QGuiApplication::font();
        font.setPixelSize(18);
        textblock::ParagraphLayout paragraph;
        paragraph.rebuild({text, {{start, text.size(), true, true}}}, font, 700, Qt::yellow);
        const QTextLayout &layout = *paragraph.textLayout();
        verifyGlyphs(layout, QStringLiteral("正文"), "OPPO Sans 4.0", QFont::Normal);
        verifyGlyphs(layout, QStringLiteral("ABC 123"), "JetBrains Mono", QFont::Normal);
        verifyGlyphs(layout, QStringLiteral("重点"), "OPPO Sans 4.0", QFont::Bold);
        verifyGlyphs(layout, QStringLiteral("XYZ 456"), "JetBrains Mono", QFont::Bold);
    }

    void adjacentScriptsAndPunctuation()
    {
        QFont font = QGuiApplication::font();
        font.setPixelSize(18);
        textblock::ParagraphLayout paragraph;
        paragraph.rebuild({QStringLiteral("中文123中文ABC中文,!?中文；ＡＢＣ１２３，。！？English"), {}},
                          font, 1000, Qt::yellow);
        const QTextLayout &layout = *paragraph.textLayout();
        verifyGlyphs(layout, QStringLiteral("123"), "JetBrains Mono", QFont::Normal);
        verifyGlyphs(layout, QStringLiteral("ABC"), "JetBrains Mono", QFont::Normal);
        verifyGlyphs(layout, QStringLiteral(",!?"), "JetBrains Mono", QFont::Normal);
        verifyGlyphs(layout, QStringLiteral("ＡＢＣ１２３，。！？"), "OPPO Sans 4.0", QFont::Normal);
    }

    void sharedSymbolsKeepOriginalFont()
    {
        // Preserve the original fonts: shared symbols are intentionally served
        // by JetBrains Mono even when they appear inside Chinese text.
        for (const auto weight : {QFont::Normal, QFont::DemiBold, QFont::Bold}) {
            QFont font = QGuiApplication::font();
            font.setPixelSize(18);
            font.setWeight(weight);
            textblock::ParagraphLayout paragraph;
            paragraph.rebuild({QStringLiteral("中文｛重点｝中文，。！？“引号”…— {}"), {}},
                              font, 1000, Qt::yellow);
            const QTextLayout &layout = *paragraph.textLayout();
            verifyGlyphs(layout, QStringLiteral("中文"), "OPPO Sans 4.0", weight);
            verifyGlyphs(layout, QStringLiteral("，。！？"), "OPPO Sans 4.0", weight);
            for (const QString &symbol : {QStringLiteral("｛"), QStringLiteral("｝"),
                                          QStringLiteral("“"), QStringLiteral("”"),
                                          QStringLiteral("…"), QStringLiteral("—"),
                                          QStringLiteral("{}")}) {
                verifyGlyphs(layout, symbol, "JetBrains Mono", weight);
            }
        }
    }

    void qmlAndControlsInheritApplicationFont()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            import TextBlock
            Window {
                Text { objectName: "text"; font.pixelSize: 18; text: "中文 ABC 123" }
                Text { objectName: "valueFont"; font: Qt.font({pixelSize: 18}); text: "中文 ABC 123" }
                Button { objectName: "button"; text: "按钮 ABC 123" }
                ComboBox { objectName: "combo"; model: ["中文 ABC 123"] }
                Switch { objectName: "switch"; text: "开关 ABC 123" }
                MenuItem { objectName: "menuItem"; text: "复制 ABC 123" }
                ToolTip { objectName: "toolTip"; text: "提示 ABC 123" }
                TextBlock { objectName: "block"; content: ({text:"中文 ABC 123", ranges:[]}) }
            }
        )", QUrl("qrc:/application_fonts_test.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        for (const QString &name : {"text", "valueFont", "button", "combo", "switch",
                                    "menuItem", "toolTip", "block"}) {
            const auto *item = root->findChild<QObject *>(name);
            QVERIFY2(item, qPrintable(name));
            const QFont font = item->property("font").value<QFont>();
            QVERIFY2(font.families() == QGuiApplication::font().families(),
                     qPrintable(name + ": " + font.families().join(", ")));
        }
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (!ChunfeFonts::initialize())
        return 1;
    ApplicationFontsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "application_fonts_tests.moc"
