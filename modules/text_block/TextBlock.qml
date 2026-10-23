import QtQuick
// Explicit self-import also exposes C++ types to Qt Creator's classic code model.
import TextBlock

Item {
    id: root

    property alias content: paragraph.content
    property alias font: paragraph.font
    property real maximumWidth: 480
    property real padding: 0
    property Component background: null
    property alias textColor: paragraph.color
    property alias highlightColor: paragraph.highlightColor

    readonly property int lineCount: paragraph.lineCount
    readonly property int layoutCount: paragraph.layoutCount
    readonly property real layoutMicroseconds: paragraph.layoutMicroseconds
    readonly property real effectiveMaximumWidth: Number.isFinite(maximumWidth) ? Math.max(0, maximumWidth) : 0
    readonly property real effectivePadding: Math.min(Number.isFinite(padding) ? Math.max(0, padding) : 0,
                                                     effectiveMaximumWidth / 2)

    implicitWidth: Math.min(effectiveMaximumWidth, paragraph.implicitWidth + 2 * effectivePadding)
    implicitHeight: paragraph.implicitHeight + 2 * effectivePadding
    Accessible.role: Accessible.StaticText
    Accessible.name: String(content.text ?? "")

    Loader {
        anchors.fill: parent
        z: -1
        sourceComponent: root.background
    }

    ParagraphItem {
        id: paragraph
        x: root.effectivePadding
        y: root.effectivePadding
        maximumWidth: Math.max(0, root.effectiveMaximumWidth - 2 * root.effectivePadding)
        width: implicitWidth
        height: implicitHeight
    }
}
