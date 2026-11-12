#include "cf_log.h"

#include <QtLogging>

#include <mutex>

namespace {
std::once_flag handlerOnce;
std::mutex handlerMutex;
QtMessageHandler previousHandler = nullptr;

void shortenFileName(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QtMessageHandler handler;
    {
        // Initialization installs the handler before it can publish the previous one.
        std::lock_guard<std::mutex> lock(handlerMutex);
        handler = previousHandler;
    }

    const char *fileName = context.file;
    if (context.file) {
        for (const char *p = context.file; *p; ++p) {
            if (*p == '/' || *p == '\\')
                fileName = p + 1;
        }
    }
    if (fileName == context.file) {
        handler(type, context, message);
        return;
    }

    const QMessageLogContext shortened(fileName, context.line, context.function, context.category);
    handler(type, shortened, message);
}
}

void CfLog::initialize()
{
    qSetMessagePattern(QStringLiteral(
        "%{time yyyy-MM-dd hh:mm:ss.zzz} "
        "[%{if-debug}DEBUG%{endif}%{if-info}INFO%{endif}"
        "%{if-warning}WARNING%{endif}%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif}] "
        "[%{category}] %{file}:%{line} %{message}"));
    std::call_once(handlerOnce, [] {
        std::lock_guard<std::mutex> lock(handlerMutex);
        previousHandler = qInstallMessageHandler(shortenFileName);
    });
}
