#pragma once

#include "paragraph_layout.h"
#include "text_block_content.h"

#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// Implementation item for TextBlock.qml. Consumers use the complete TextBlock.
class ParagraphItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap content READ content WRITE setContent NOTIFY contentChanged FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
    Q_PROPERTY(qreal maximumWidth READ maximumWidth WRITE setMaximumWidth NOTIFY maximumWidthChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    Q_PROPERTY(QColor highlightColor READ highlightColor WRITE setHighlightColor NOTIFY highlightColorChanged FINAL)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY layoutChanged FINAL)
    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal layoutMicroseconds READ layoutMicroseconds NOTIFY layoutChanged FINAL)

public:
    explicit ParagraphItem(QQuickItem *parent = nullptr);

    QVariantMap content() const { return m_content; }
    void setContent(const QVariantMap &value);
    QFont font() const { return m_font; }
    void setFont(const QFont &value);
    qreal maximumWidth() const { return m_maximumWidth; }
    void setMaximumWidth(qreal value);
    QColor color() const { return m_color; }
    void setColor(const QColor &value);
    QColor highlightColor() const { return m_highlightColor; }
    void setHighlightColor(const QColor &value);
    int lineCount() const { return m_layout.lineCount(); }
    int layoutCount() const { return m_layoutCount; }
    qreal layoutMicroseconds() const { return m_layoutMicroseconds; }

signals:
    void contentChanged();
    void fontChanged();
    void maximumWidthChanged();
    void colorChanged();
    void highlightColorChanged();
    void layoutChanged();

protected:
    void componentComplete() override;
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void scheduleLayout();

    QVariantMap m_content;
    textblock::Content m_parsedContent;
    textblock::ParagraphLayout m_layout;
    QFont m_font;
    qreal m_maximumWidth = 456;
    QColor m_color = QColor("#243247");
    QColor m_highlightColor = QColor("#ffe5a3");
    bool m_layoutDirty = true;
    bool m_renderDirty = true;
    int m_layoutCount = 0;
    qreal m_layoutMicroseconds = 0;
};
