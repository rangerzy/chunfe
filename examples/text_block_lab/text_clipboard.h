#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// Clipboard access is an application service, separate from text rendering.
class TextClipboard : public QObject
{
    Q_OBJECT
    QML_ELEMENT
public:
    using QObject::QObject;
    Q_INVOKABLE bool copyText(const QString &text) const;
};
