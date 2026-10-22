#include "experiment_metrics.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

Q_IMPORT_QML_PLUGIN(TextBlockPlugin)

class ExperimentMetricsTests : public QObject
{
    Q_OBJECT
private slots:
    void rejectsInvalidOrThrowingMutations()
    {
        ExperimentMetrics metrics;
        QSignalSpy errors(&metrics, &ExperimentMetrics::failed);
        metrics.measureBatch(nullptr, {});
        QCOMPARE(errors.count(), 1);

        QQmlEngine engine;
        QQuickWindow window;
        auto throws = engine.evaluate("(function() { throw new Error('probe error'); })");
        metrics.measureBatch(window.contentItem(), throws);
        QCOMPARE(errors.count(), 2);
        QVERIFY(errors.last().first().toString().contains("probe error"));
    }

    void flushesDeferredLayoutAndMeasuresAFrame()
    {
        QQmlEngine engine;
        QQuickWindow window;
        window.resize(600, 400);
        window.show();
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import TextBlock
            TextBlock { maximumWidth: 500; font.pixelSize: 18; content: ({text: "before", ranges: []}) }
        )", QUrl("qrc:/metrics_test.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        auto *block = qobject_cast<QQuickItem *>(component.create());
        QVERIFY(block);
        block->setParent(window.contentItem());
        block->setParentItem(window.contentItem());
        QQmlEngine::setObjectOwnership(block, QQmlEngine::CppOwnership);
        engine.globalObject().setProperty("block", engine.newQObject(block));
        auto mutation = engine.evaluate(R"((function() {
            block.maximumWidth = 120;
            block.content = {text: "A complete paragraph with multiple words to wrap.", ranges: []};
        }))");
        ExperimentMetrics metrics;
        QSignalSpy measured(&metrics, &ExperimentMetrics::batchMeasured);
        QSignalSpy errors(&metrics, &ExperimentMetrics::failed);
        metrics.measureBatch(block, mutation);
        QVERIFY(block->property("lineCount").toInt() > 1);
        QVERIFY(block->width() <= 120);
        QTRY_COMPARE_WITH_TIMEOUT(measured.count(), 1, 3000);
        QCOMPARE(errors.count(), 0);
        const auto sample = measured.first();
        QVERIFY(sample[0].toDouble() >= 0);
        QVERIFY(sample[1].toDouble() >= sample[0].toDouble() / 1000.0);
        QVERIFY(sample[2].toDouble() >= 0);
        QVERIFY(sample[3].toBool());

        // A canceled batch must not deliver a stale result into the next run.
        metrics.measureBatch(block, mutation);
        metrics.cancel();
        QTest::qWait(80);
        QCOMPARE(measured.count(), 1);
        metrics.measureBatch(block, mutation);
        QTRY_COMPARE_WITH_TIMEOUT(measured.count(), 2, 3000);
        QCOMPARE(errors.count(), 0);

        QQmlEngine::setObjectOwnership(&metrics, QQmlEngine::CppOwnership);
        engine.globalObject().setProperty("metrics", engine.newQObject(&metrics));
        metrics.measureBatch(block, engine.evaluate("(function() { metrics.cancel(); })"));
        QTest::qWait(80);
        QCOMPARE(measured.count(), 2);
        metrics.measureBatch(block, mutation);
        QTRY_COMPARE_WITH_TIMEOUT(measured.count(), 3, 3000);
    }
};

QTEST_MAIN(ExperimentMetricsTests)
#include "experiment_metrics_tests.moc"
