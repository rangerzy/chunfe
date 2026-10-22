#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QTest>

#include <memory>
#include "paragraph_item.h"

Q_IMPORT_QML_PLUGIN(TextBlockPlugin)

class TextBlockTests : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQuickWindow> window;

    QQuickItem *createItem(const QByteArray &source, const QVariantMap &properties = {})
    {
        QQmlComponent component(engine.get());
        component.setData(source, QUrl("qrc:/text_block_test.qml"));
        if (!component.isReady()) {
            QTest::qFail(qPrintable(component.errorString()), __FILE__, __LINE__);
            return nullptr;
        }
        auto *item = qobject_cast<QQuickItem *>(component.createWithInitialProperties(properties));
        if (item) {
            item->setParent(window->contentItem());
            item->setParentItem(window->contentItem());
        }
        return item;
    }

    QQuickItem *block(const QString &text, qreal maximumWidth = 320,
                      const QVariantList &ranges = {})
    {
        return createItem(R"(
            import QtQuick
            import TextBlock
            TextBlock { font.pixelSize: 20; padding: 12 }
        )", {{"content", QVariantMap{{"text", text}, {"ranges", ranges}}},
             {"maximumWidth", maximumWidth}});
    }

    int highlightedPixels()
    {
        window->requestUpdate();
        QTest::qWait(40);
        const QImage frame = window->grabWindow();
        if (frame.isNull()) {
            QTest::qFail("The scene must produce a rendered frame", __FILE__, __LINE__);
            return -1;
        }
        int count = 0;
        for (int y = 0; y < frame.height(); ++y) {
            for (int x = 0; x < frame.width(); ++x) {
                const QColor pixel = frame.pixelColor(x, y);
                if (pixel.red() > 230 && pixel.green() < 30 && pixel.blue() > 230)
                    ++count;
            }
        }
        return count;
    }

private slots:
    void init()
    {
        engine = std::make_unique<QQmlEngine>();
        window = std::make_unique<QQuickWindow>();
        window->resize(900, 700);
        window->setColor(Qt::white);
        window->show();
    }

    void cleanup()
    {
        window.reset();
        engine.reset();
    }

    void importsAndSizesShortText()
    {
        auto *item = block(QStringLiteral("一段文字"));
        QVERIFY(item);
        QTRY_VERIFY(item->width() > 24);
        QVERIFY(item->width() < 200);
        QVERIFY(item->height() > 24);
        QTRY_COMPARE(item->property("lineCount").toInt(), 1);
    }

    void defaultGeometryHugsTextWithoutPadding()
    {
        auto *item = createItem(R"(
            import QtQuick
            import TextBlock
            TextBlock { content: ({text: "Hello 世界"}); font.pixelSize: 20 }
        )");
        QVERIFY(item);
        auto *paragraph = item->findChild<ParagraphItem *>();
        QVERIFY(paragraph);
        QTRY_VERIFY(item->property("layoutCount").toInt() > 0);
        QCOMPARE(item->width(), paragraph->width());
        QCOMPARE(item->height(), paragraph->height());
        QCOMPARE(paragraph->position(), QPointF(0, 0));
        const QSizeF bareSize(item->width(), item->height());
        item->setProperty("padding", 16);
        QTRY_COMPARE(item->width(), bareSize.width() + 32);
        QCOMPARE(item->height(), bareSize.height() + 32);
        QCOMPARE(paragraph->position(), QPointF(16, 16));
    }

    void noBackgroundLeavesTheCallerVisible()
    {
        window->setColor(QColor("#126e98"));
        auto *item = block(QStringLiteral("Text only"));
        QVERIFY(item);
        QTRY_VERIFY(item->height() > 24);
        window->requestUpdate();
        QTest::qWait(40);
        const QImage frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        const QPointF sample(item->width() / 2, item->height() - 6);
        QCOMPARE(frame.pixelColor((sample * frame.devicePixelRatio()).toPoint()), QColor("#126e98"));
    }

    void callerHandlesClicksWithoutLosingFocus()
    {
        auto *host = createItem(R"(
            import QtQuick
            import TextBlock
            Item {
                width: 360; height: 120
                property int leftClicks: 0
                property int rightClicks: 0
                Item { objectName: "focusOwner"; focus: true }
                TextBlock {
                    content: ({text: "Caller owns all input"})
                    font.pixelSize: 20
                    padding: 12
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onTapped: function(eventPoint, button) {
                        if (button === Qt.LeftButton) parent.leftClicks++;
                        else parent.rightClicks++;
                    }
                }
            }
        )");
        QVERIFY(host);
        auto *focusOwner = host->findChild<QQuickItem *>("focusOwner");
        QVERIFY(focusOwner);
        focusOwner->forceActiveFocus();
        QTRY_VERIFY(focusOwner->hasActiveFocus());
        QTest::mouseClick(window.get(), Qt::LeftButton, Qt::NoModifier, QPoint(30, 20));
        QTRY_COMPARE(host->property("leftClicks").toInt(), 1);
        QVERIFY(focusOwner->hasActiveFocus());
        QTest::mouseClick(window.get(), Qt::RightButton, Qt::NoModifier, QPoint(30, 20));
        QTRY_COMPARE(host->property("rightClicks").toInt(), 1);
        QVERIFY(focusOwner->hasActiveFocus());
    }

    void backgroundFollowsGeometryWithoutChangingTextLayout()
    {
        auto *item = block(QStringLiteral("Text above background"), 220);
        QVERIFY(item);
        item->setProperty("textColor", QColor("#ff0000"));
        QTRY_VERIFY(item->property("layoutCount").toInt() > 0);
        const QSizeF originalSize(item->width(), item->height());
        const int originalLayouts = item->property("layoutCount").toInt();
        QQmlComponent background(engine.get());
        background.setData(R"(
            import QtQuick
            Rectangle {
                objectName: "callerBackground"
                implicitWidth: 1000; implicitHeight: 1000
                color: Qt.rgba(18 / 255, 110 / 255, 152 / 255, 1)
            }
        )", QUrl("qrc:/caller_background.qml"));
        QVERIFY2(background.isReady(), qPrintable(background.errorString()));
        QVERIFY(item->setProperty("background", QVariant::fromValue(&background)));
        QTRY_VERIFY(item->findChild<QQuickItem *>("callerBackground"));
        QPointer<QQuickItem> firstBackground = item->findChild<QQuickItem *>("callerBackground");
        QTRY_COMPARE(QSizeF(firstBackground->width(), firstBackground->height()), originalSize);
        QCOMPARE(QSizeF(item->width(), item->height()), originalSize);
        QCOMPARE(item->property("layoutCount").toInt(), originalLayouts);
        window->requestUpdate();
        QTest::qWait(40);
        const QImage frame = window->grabWindow();
        QVERIFY(!frame.isNull());
        QCOMPARE(frame.pixelColor(QPoint(2, 2) * frame.devicePixelRatio()), QColor("#126e98"));
        int redPixels = 0;
        for (int y = 0; y < frame.height(); ++y)
            for (int x = 0; x < frame.width(); ++x) {
                const QColor color = frame.pixelColor(x, y);
                if (color.red() > 200 && color.green() < 60 && color.blue() < 60)
                    ++redPixels;
            }
        QVERIFY2(redPixels > 20, "The background must stay behind the text");
        item->setProperty("maximumWidth", 100);
        QTRY_VERIFY(item->height() > originalSize.height());
        QCOMPARE(firstBackground->width(), item->width());
        QCOMPARE(firstBackground->height(), item->height());
        QQmlComponent replacement(engine.get());
        replacement.setData(R"(
            import QtQuick
            Rectangle { objectName: "replacementBackground"; color: "lightgreen" }
        )", QUrl("qrc:/replacement_background.qml"));
        QVERIFY2(replacement.isReady(), qPrintable(replacement.errorString()));
        QVERIFY(item->setProperty("background", QVariant::fromValue(&replacement)));
        QTRY_VERIFY(firstBackground.isNull());
        QPointer<QQuickItem> secondBackground = item->findChild<QQuickItem *>("replacementBackground");
        QVERIFY(secondBackground);
        QCOMPARE(secondBackground->width(), item->width());
        const QSizeF withBackground(item->width(), item->height());
        QVERIFY(item->setProperty("background", QVariant::fromValue<QQmlComponent *>(nullptr)));
        QTRY_VERIFY(secondBackground.isNull());
        QCOMPARE(QSizeF(item->width(), item->height()), withBackground);
    }

    void wrapsAndExpandsAgain()
    {
        auto *item = block(QStringLiteral("这是一段用于验证最大宽度和自动换行的文本。"), 150);
        QVERIFY(item);
        QTRY_VERIFY(item->property("lineCount").toInt() > 1);
        QVERIFY(item->width() <= 150);
        const qreal wrappedHeight = item->height();
        item->setProperty("maximumWidth", 800);
        QTRY_COMPARE(item->property("lineCount").toInt(), 1);
        QVERIFY(item->height() < wrappedHeight);
        QVERIFY(item->width() > 150);
    }

    void streamsOnlyTheChangedBlock()
    {
        auto *changing = block(QStringLiteral("开始"), 160);
        auto *stable = block(QStringLiteral("独立胶囊"), 160);
        QVERIFY(changing && stable);
        stable->setY(200);
        QTRY_VERIFY(changing->property("layoutCount").toInt() > 0);
        QTRY_VERIFY(stable->property("layoutCount").toInt() > 0);
        const int stableCount = stable->property("layoutCount").toInt();
        const int changingCount = changing->property("layoutCount").toInt();
        const QVariantMap snapshot{{"text", QStringLiteral("逐步追加文字，达到宽度上限后自动换行。")},
                                   {"ranges", QVariantList{QVariantMap{{"start", 0}, {"end", 4}, {"bold", true}}}}};
        changing->setProperty("content", snapshot);
        QTRY_VERIFY(changing->property("lineCount").toInt() > 1);
        QVERIFY(changing->property("layoutCount").toInt() > changingCount);
        QCOMPARE(stable->property("layoutCount").toInt(), stableCount);
        const int afterUpdate = changing->property("layoutCount").toInt();
        changing->setProperty("content", snapshot);
        QTest::qWait(40);
        QCOMPARE(changing->property("layoutCount").toInt(), afterUpdate);
    }

    void emptyAndVeryNarrowContentStayFinite()
    {
        auto *item = block(QString(), 0);
        QVERIFY(item);
        QTest::qWait(30);
        QVERIFY(qIsFinite(item->width()) && item->width() >= 0);
        QVERIFY(qIsFinite(item->height()) && item->height() >= 0);
        item->setProperty("content", QVariantMap{{"text", QString::fromUtf8("👩🏽‍💻中文Supercalifragilistic")}});
        item->setProperty("maximumWidth", 60);
        QTRY_VERIFY(item->property("lineCount").toInt() > 1);
        QVERIFY(item->width() <= 60);
        QVERIFY(qIsFinite(item->height()));
    }

    void styleOnlyUpdatesReachTheSceneGraph()
    {
        const QString text = QStringLiteral("重点标记跨越换行的文字示例");
        auto *item = block(text, 150);
        QVERIFY(item);
        item->setProperty("highlightColor", QColor("#ff00ff"));
        QTRY_VERIFY(item->property("lineCount").toInt() > 1);
        QCOMPARE(highlightedPixels(), 0);
        item->setProperty("content", QVariantMap{{"text", text},
            {"ranges", QVariantList{
                QVariantMap{{"start", 0}, {"end", text.size()}, {"highlight", true}},
                QVariantMap{{"start", 2}, {"end", 8}, {"bold", true}}}}});
        QTRY_VERIFY(highlightedPixels() > 50);
        item->setProperty("content", QVariantMap{{"text", text}});
        QTRY_COMPARE(highlightedPixels(), 0);
    }

    void graphemeSplittingRangesAreIgnored()
    {
        const QString text = QString::fromUtf8("A👩🏽‍💻B");
        auto *item = block(text, 320, {QVariantMap{{"start", 1}, {"end", 3}, {"highlight", true}}});
        QVERIFY(item);
        item->setProperty("highlightColor", QColor("#ff00ff"));
        QTRY_COMPARE(item->property("lineCount").toInt(), 1);
        QCOMPARE(highlightedPixels(), 0);
        item->setProperty("content", QVariantMap{{"text", text}, {"ranges", QVariantList{
            QVariantMap{{"start", 1}, {"end", 8}, {"highlight", true}}}}});
        QTRY_VERIFY(highlightedPixels() > 10);
    }

    void recreatesNodesAfterWindowResourcesAreReleased()
    {
        auto *item = block(QStringLiteral("重新显示"), 200,
                           {QVariantMap{{"start", 0}, {"end", 4}, {"highlight", true}}});
        QVERIFY(item);
        item->setProperty("highlightColor", QColor("#ff00ff"));
        QTRY_VERIFY(highlightedPixels() > 20);
        window->setPersistentSceneGraph(false);
        window->setPersistentGraphics(false);
        window->hide();
        window->releaseResources();
        QTest::qWait(40);
        window->show();
        QTRY_VERIFY(highlightedPixels() > 20);
    }
};

QTEST_MAIN(TextBlockTests)
#include "text_block_tests.moc"
