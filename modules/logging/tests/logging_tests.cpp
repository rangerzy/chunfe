#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QtTest>

class LoggingTests : public QObject
{
    Q_OBJECT

private:
    QByteArray runProbe(const QString &mode, const QString &pattern = QString())
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove(QStringLiteral("QT_MESSAGE_PATTERN"));
        env.remove(QStringLiteral("QT_LOGGING_RULES"));
        env.remove(QStringLiteral("QT_LOGGING_CONF"));
        env.remove(QStringLiteral("QT_FATAL_WARNINGS"));
        env.remove(QStringLiteral("QT_FATAL_CRITICALS"));
        if (!pattern.isNull())
            env.insert(QStringLiteral("QT_MESSAGE_PATTERN"), pattern);
        process.setProcessEnvironment(env);
        process.start(QString::fromUtf8(CF_LOG_PROBE_PATH), {mode});
        if (!process.waitForFinished(10000) || process.exitCode() != 0)
            qFatal("log probe did not finish successfully");
        return process.readAllStandardError() + process.readAllStandardOutput();
    }

private slots:
    void includesLevelCategoryAndCallSite()
    {
        const QString output = QString::fromUtf8(runProbe(QStringLiteral("format")));
        const auto line = QRegularExpression(QStringLiteral(
            R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[INFO\] \[logging_test\] log_probe\.cpp:\d+ info message)"));
        QVERIFY2(line.match(output).hasMatch(), qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("[DEBUG] [logging_test]")), qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("[WARNING] [logging_test]")), qPrintable(output));
        QVERIFY2(output.contains(QStringLiteral("[ERROR] [logging_test]")), qPrintable(output));
    }

    void filteredExpressionsAreNotEvaluated()
    {
        const QByteArray output = runProbe(QStringLiteral("filter"));
        QVERIFY2(output.contains("evaluated 0"), output.constData());
    }

    void categoriesAreIsolatedAcrossTargets()
    {
        const QByteArray output = runProbe(QStringLiteral("targets"));
        QVERIFY2(output.contains("[chunfe_log_test_alpha]"), output.constData());
        QVERIFY2(output.contains("[beta]"), output.constData());
    }

    void qtMessagesUseBasenamesForBothPathSeparators()
    {
        const QByteArray output = runProbe(QStringLiteral("paths"));
        QVERIFY2(output.contains("[INFO] [foreign] example.cpp:42 posix message"), output.constData());
        QVERIFY2(output.contains("[INFO] [foreign] other.cpp:43 windows message"), output.constData());
        QVERIFY2(output.contains("missing file"), output.constData());
        QVERIFY2(!output.contains("/tmp/project"), output.constData());
        QVERIFY2(!output.contains("C:\\work"), output.constData());
    }

    void environmentPatternReceivesBasename()
    {
        const QByteArray output = runProbe(QStringLiteral("paths"), QStringLiteral("OVERRIDE %{file}:%{line} %{message}"));
        QVERIFY2(output.contains("OVERRIDE example.cpp:42 posix message"), output.constData());
        QVERIFY2(output.contains("OVERRIDE other.cpp:43 windows message"), output.constData());
    }

    void repeatedInitializationDoesNotDuplicateMessages()
    {
        const QByteArray output = runProbe(QStringLiteral("repeat"));
        QCOMPARE(output.count("only once"), 1);
    }

    void previousHandlerReceivesMessageAndOriginalMetadata()
    {
        const QByteArray output = runProbe(QStringLiteral("previous"));
        QCOMPARE(output.count("forwarded message"), 1);
        QVERIFY2(output.contains("previous type=" + QByteArray::number(int(QtWarningMsg))), output.constData());
        QVERIFY2(output.contains("category=foreign file=example.cpp line=42 function=function message=forwarded message"), output.constData());
    }

    void environmentCanOverrideFormat()
    {
        const QByteArray output = runProbe(QStringLiteral("format"), QStringLiteral("OVERRIDE %{message}"));
        QVERIFY2(output.contains("OVERRIDE info message"), output.constData());
        QVERIFY2(!output.contains("[INFO]"), output.constData());
    }
};

QTEST_GUILESS_MAIN(LoggingTests)
#include "logging_tests.moc"
