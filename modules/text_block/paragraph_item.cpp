#include "paragraph_item.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGTextNode>

#include <algorithm>
#include <cmath>

ParagraphItem::ParagraphItem(QQuickItem *parent)
    : QQuickItem(parent), m_font(QGuiApplication::font())
{
    setFlag(ItemHasContents);
    setClip(true);
    m_font.setPixelSize(17);
}

void ParagraphItem::setContent(const QVariantMap &value)
{
    if (m_content == value)
        return;
    m_content = value;
    const auto parsed = textblock::Content::fromVariant(value);
    if (m_parsedContent != parsed) {
        m_parsedContent = parsed;
        scheduleLayout();
    }
    emit contentChanged();
}

void ParagraphItem::setFont(const QFont &value)
{
    if (m_font == value)
        return;
    m_font = value;
    scheduleLayout();
    emit fontChanged();
}

void ParagraphItem::setMaximumWidth(qreal value)
{
    value = std::isfinite(value) ? std::max(qreal(0), value) : 0;
    if (m_maximumWidth == value)
        return;
    m_maximumWidth = value;
    scheduleLayout();
    emit maximumWidthChanged();
}

void ParagraphItem::setColor(const QColor &value)
{
    if (m_color == value)
        return;
    m_color = value;
    m_renderDirty = true;
    update();
    emit colorChanged();
}

void ParagraphItem::setHighlightColor(const QColor &value)
{
    if (m_highlightColor == value)
        return;
    m_highlightColor = value;
    scheduleLayout();
    emit highlightColorChanged();
}

void ParagraphItem::scheduleLayout()
{
    m_layoutDirty = true;
    polish();
}

void ParagraphItem::componentComplete()
{
    QQuickItem::componentComplete();
    scheduleLayout();
}

void ParagraphItem::updatePolish()
{
    if (!m_layoutDirty)
        return;
    QElapsedTimer timer;
    timer.start();
    m_layout.rebuild(m_parsedContent, m_font, m_maximumWidth, m_highlightColor);
    m_layoutMicroseconds = timer.nsecsElapsed() / 1000.0;
    ++m_layoutCount;
    m_layoutDirty = false;
    setImplicitSize(m_layout.size().width(), m_layout.size().height());
    m_renderDirty = true;
    update();
    emit layoutChanged();
}

QSGNode *ParagraphItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Qt blocks the GUI thread during this sync. Layouts are only changed in
    // updatePolish; QSG nodes are only touched here on the rendering thread.
    if (!window() || m_layout.lineCount() == 0 || m_layout.size().width() <= 0) {
        delete oldNode;
        return nullptr;
    }
    auto *node = static_cast<QSGTextNode *>(oldNode);
    if (!node) {
        node = window()->createTextNode();
        m_renderDirty = true;
    }
    if (m_renderDirty) {
        node->clear();
        node->setColor(m_color);
        node->setRenderType(QSGTextNode::QtRendering);
        node->addTextLayout(QPointF(0, 0), m_layout.textLayout());
        m_renderDirty = false;
    }
    return node;
}
