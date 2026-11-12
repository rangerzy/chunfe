import QtQuick
import QtQuick.Layouts
import Chunfe.Ui

Rectangle {
    id: root
    default property alias cardContent: contentColumn.data
    property bool framed: true
    property real padding: framed ? 20 : 0
    property real spacing: 16
    readonly property real bodyWidth: Math.max(0, width - 2 * padding)

    color: framed ? Theme.background : "transparent"
    border.color: Theme.border
    border.width: framed ? 1 : 0
    radius: 14
    implicitHeight: contentColumn.implicitHeight + 2 * padding

    ColumnLayout {
        id: contentColumn
        x: root.padding
        y: root.padding
        width: root.bodyWidth
        spacing: root.spacing
    }
}
