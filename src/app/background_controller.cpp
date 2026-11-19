#include "background_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

#include <utility>

namespace {
constexpr qint64 MaxMarkdownBytes = 256 * 1024;
}

BackgroundController::BackgroundController(QString dataDirectory, QObject *parent)
    : QObject(parent),
      m_managedFilePath(dataDirectory.isEmpty()
                            ? QString()
                            : QDir(dataDirectory).absoluteFilePath(QStringLiteral("background.md")))
{
    if (m_managedFilePath.isEmpty()) {
        m_errorMessage = tr("无法访问背景资料目录。");
        return;
    }

    const QFileInfo managed(m_managedFilePath);
    if (managed.isSymLink()) {
        m_errorMessage = tr("无法读取已保存的背景资料，请检查配置目录。");
    } else if (managed.exists()) {
        QByteArray bytes;
        QString text;
        m_errorMessage = readMarkdown(m_managedFilePath, bytes, text);
        if (m_errorMessage.isEmpty())
            m_backgroundText = std::move(text);
    }
}

bool BackgroundController::importMarkdown(const QUrl &source)
{
    if (source.isEmpty())
        return false;
    if (!source.isLocalFile())
        return rejectImport(tr("请选择本地 Markdown 文件。"));

    const QString sourcePath = source.toLocalFile();
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    if (suffix != QStringLiteral("md") && suffix != QStringLiteral("markdown"))
        return rejectImport(tr("请选择 .md 或 .markdown 文件。"));

    QByteArray bytes;
    QString text;
    const QString error = readMarkdown(sourcePath, bytes, text);
    if (!error.isEmpty())
        return rejectImport(error);

    const QFileInfo target(m_managedFilePath);
    const auto saveError = tr("无法保存背景资料，请检查本地存储后重试。");
    if (m_managedFilePath.isEmpty() || target.isSymLink() || !QDir().mkpath(target.absolutePath()))
        return rejectImport(saveError);

    QSaveFile destination(m_managedFilePath);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size() || !destination.commit())
        return rejectImport(saveError);

    const bool textChanged = m_backgroundText != text;
    const bool errorChanged = !m_errorMessage.isEmpty();
    m_backgroundText = std::move(text);
    m_errorMessage.clear();
    if (textChanged)
        emit backgroundTextChanged();
    if (errorChanged)
        emit errorMessageChanged();
    return true;
}

QString BackgroundController::readMarkdown(const QString &path, QByteArray &bytes, QString &text)
{
    const auto readError = tr("无法读取 Markdown 文件，请重新选择。");
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly))
        return readError;
    bytes = file.read(MaxMarkdownBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return readError;
    if (bytes.size() > MaxMarkdownBytes)
        return tr("请选择不超过 256 KiB 的 Markdown 文件。");

    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    text = decoder(bytes);
    if (decoder.hasError() || bytes.contains('\0'))
        return tr("背景资料须为 UTF-8 编码的 Markdown 文本。");
    if (text.trimmed().isEmpty())
        return tr("Markdown 文件内容为空，请重新选择。");
    return {};
}

bool BackgroundController::rejectImport(const QString &error)
{
    if (m_errorMessage != error) {
        m_errorMessage = error;
        emit errorMessageChanged();
    }
    return false;
}
