// Kept local so this standalone example needs only its own sources, TextBlock and Qt.
#include "text_clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

bool TextClipboard::copyText(const QString &text) const
{
    auto *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(text);
    return true;
}
