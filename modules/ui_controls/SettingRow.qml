import QtQuick
import QtQuick.Layouts
import Chunfe.Ui

Item {
    id: root
    default property alias fieldContent: fields.data
    property string label: ""
    property string description: ""
    property real labelWidth: 136
    property real spacing: 20
    property real minimumHeight: 42
    readonly property real controlWidth: fields.width
    readonly property real fieldHeight: Math.max(38, fields.implicitHeight)

    implicitHeight: Math.max(minimumHeight, caption.implicitHeight, fieldHeight) + (description ? 6 + help.implicitHeight : 0)

    Text {
        id: caption
        width: root.labelWidth
        y: Math.max(0, (root.fieldHeight - implicitHeight) / 2)
        text: root.label
        color: Theme.text
        font.pixelSize: 14
        font.weight: Font.Medium
        wrapMode: Text.Wrap
    }
    ColumnLayout {
        id: fields
        x: root.labelWidth + root.spacing
        y: (root.fieldHeight - implicitHeight) / 2
        width: Math.max(0, root.width - x)
        spacing: 8
    }
    Text {
        id: help
        x: fields.x
        y: Math.max(root.minimumHeight, root.fieldHeight) + 6
        width: fields.width
        visible: root.description !== ""
        text: root.description
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }
}
