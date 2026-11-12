#include "cf_log.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <cstdio>

void emitAlpha();
void emitBeta();

static void previousHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QByteArray text = message.toUtf8();
    std::fprintf(stderr, "previous type=%d category=%s file=%s line=%d function=%s message=%s\n",
                 int(type), context.category ? context.category : "<null>",
                 context.file ? context.file : "<null>", context.line,
                 context.function ? context.function : "<null>", text.constData());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("previous")))
        qInstallMessageHandler(previousHandler);
    CfLog::initialize();

    if (app.arguments().contains(QStringLiteral("paths"))) {
        QMessageLogger("/tmp/project/sub/example.cpp", 42, "function", "foreign")
            .info() << "posix message";
        QMessageLogger("C:\\work\\sub\\other.cpp", 43, "function", "foreign")
            .info() << "windows message";
        QMessageLogger(nullptr, 44, "function", "foreign").info() << "missing file";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("repeat"))) {
        CfLog::initialize();
        INFO_CF_LOG() << "only once";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("previous"))) {
        QMessageLogger("/tmp/project/sub/example.cpp", 42, "function", "foreign")
            .warning() << "forwarded message";
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("filter"))) {
        QLoggingCategory::setFilterRules(QStringLiteral("logging_test.debug=false"));
        int evaluated = 0;
        DEBUG_CF_LOG() << ++evaluated;
        INFO_CF_LOG() << "evaluated" << evaluated;
        return 0;
    }

    if (app.arguments().contains(QStringLiteral("targets"))) {
        emitAlpha();
        emitBeta();
        return 0;
    }

    DEBUG_CF_LOG() << "debug message";
    INFO_CF_LOG() << "info message";
    WARNING_CF_LOG() << "warning message";
    ERROR_CF_LOG() << "error message";
}
