#include "app_fonts.h"
#include "experiment_metrics.h"
#include "paragraph_item.h"
#include "text_clipboard.h"

#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <functional>

Q_IMPORT_QML_PLUGIN(TextBlockPlugin)

class MainQmlTests : public QObject
{
    Q_OBJECT

private slots:
    void editorMetadataResolvesComponents_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("module") << QStringLiteral(TEXT_BLOCK_SOURCE_DIR "/TextBlock.qml");
        QTest::newRow("application") << QStringLiteral(TEXT_BLOCK_LAB_SOURCE_DIR "/Main.qml");
        QTest::newRow("custom-control") << QStringLiteral(TEXT_BLOCK_LAB_SOURCE_DIR "/AppActionButton.qml");
    }

    void editorMetadataResolvesComponents()
    {
        QFETCH(QString, file);
        const QString metadata = QStringLiteral(TEXT_BLOCK_LAB_BINARY_DIR "/.qt/.qmlls.build.ini");
        QVERIFY2(QFile::exists(metadata), qPrintable(metadata));
        QSettings settings(metadata, QSettings::IniFormat);
        const int size = settings.beginReadArray("workspaces");
        QString selectedRoot;
        QStringList imports, resources;
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            const QString sourceRoot = settings.value("sourcePath").toString();
            if (file.startsWith(sourceRoot + '/') && sourceRoot.size() > selectedRoot.size()) {
                selectedRoot = sourceRoot;
                imports = settings.value("importPaths").toString().split(QDir::listSeparator(), Qt::SkipEmptyParts);
                resources = settings.value("resourceFiles").toString().split(QDir::listSeparator(), Qt::SkipEmptyParts);
            }
        }
        settings.endArray();
        QVERIFY2(!selectedRoot.isEmpty(), "The editor needs a workspace for the QML file");
        QStringList arguments{"--bare", "--max-warnings", "0", "--json", "-"};
        for (const auto &path : imports)
            arguments << "-I" << path;
        for (const auto &path : resources)
            arguments << "--resource" << path;
        arguments << file;
        QProcess lint;
        lint.setProcessChannelMode(QProcess::MergedChannels);
        lint.start(QStringLiteral(TEXT_BLOCK_LAB_QMLLINT), arguments);
        QVERIFY(lint.waitForStarted());
        QVERIFY(lint.waitForFinished(10000));
        const QByteArray diagnostics = lint.readAll();
        QCOMPARE(lint.exitStatus(), QProcess::NormalExit);
        QVERIFY2(lint.exitCode() == 0, diagnostics.constData());
    }

    void pageLoadsAndRelayoutsWithoutWarnings()
    {
        QTest::failOnWarning(QRegularExpression(".*"));
        QFile source(QStringLiteral(TEXT_BLOCK_LAB_SOURCE_DIR "/Main.qml"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray qml = source.readAll();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(qml, QUrl::fromLocalFile(source.fileName()));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *context = qmlContext(root.get());
        QVERIFY(context);
        auto *manyBlocks = qobject_cast<QQuickItem *>(context->objectForName(QStringLiteral("manyBlocks")));
        auto *streamBlock = context->objectForName(QStringLiteral("streamBlock"));
        QVERIFY(manyBlocks && streamBlock);

        const auto geometryIsValid = [manyBlocks](int expectedCount) {
            QList<ParagraphItem *> paragraphs;
            const std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
                if (auto *paragraph = qobject_cast<ParagraphItem *>(item))
                    paragraphs.append(paragraph);
                for (auto *child : item->childItems())
                    collect(child);
            };
            collect(manyBlocks);
            if (paragraphs.size() != expectedCount)
                return false;
            for (const auto *paragraph : paragraphs) {
                const auto *block = paragraph->parentItem();
                if (!block || paragraph->layoutCount() == 0 || paragraph->lineCount() == 0
                    || !qIsFinite(block->width()) || !qIsFinite(block->height())
                    || block->width() <= 0 || block->height() <= 0
                    || block->width() > block->property("maximumWidth").toReal() + 0.01)
                    return false;
            }
            return true;
        };
        QTRY_VERIFY(geometryIsValid(6));
        struct Scenario { const char *mutation; int count; };
        const Scenario scenarios[] = {
            {"window.width = 780", 6},
            {"countPicker.currentIndex = 1", 30},
            {"widthSlider.value = 200", 30},
            {"countPicker.currentIndex = 2", 100},
            {"widthSlider.value = 480", 100},
            {"window.width = 1300", 100},
            {"countPicker.currentIndex = 0", 6},
            {"window.streamLength = window.streamSource.length", 6},
        };
        for (const auto &scenario : scenarios) {
            QSignalSpy rendered(window, &QQuickWindow::frameSwapped);
            QQmlExpression mutation(context, root.get(), QString::fromLatin1(scenario.mutation));
            mutation.evaluate();
            QVERIFY2(!mutation.hasError(), qPrintable(mutation.error().toString()));
            window->requestUpdate();
            QTRY_VERIFY(rendered.count() > 0);
            QTRY_VERIFY(geometryIsValid(scenario.count));
        }
        QCOMPARE(streamBlock->property("content").toMap().value("text").toString(),
                 root->property("streamSource").toString());
        auto *streamParagraph = streamBlock->findChild<ParagraphItem *>();
        QVERIFY(streamParagraph);
        QVERIFY(streamParagraph->lineCount() > 1);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (!ChunfeFonts::initialize())
        return 1;
    // Register the actual application types without linking the app executable.
    qmlRegisterType<ExperimentMetrics>("TextBlockLab", 1, 0, "ExperimentMetrics");
    qmlRegisterType<TextClipboard>("TextBlockLab", 1, 0, "TextClipboard");
    MainQmlTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "main_qml_tests.moc"
