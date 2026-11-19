import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui

ComboBox {
    id: root
    implicitHeight: 38
    implicitWidth: 220
    leftPadding: 12
    rightPadding: 32
    font.pixelSize: 14
    background: Rectangle {
        color: Theme.background
        border.color: root.activeFocus ? Theme.text : Theme.border
        radius: Theme.radius
    }
    contentItem: Text {
        text: root.displayText
        font: root.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: AppIcon {
        x: root.width - width - 11
        y: (root.height - height) / 2
        name: "chevron-right"
        size: 14
        rotation: 90
        color: Theme.muted
    }
}
