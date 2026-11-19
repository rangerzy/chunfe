import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui

TextField {
    id: root

    implicitWidth: 240
    implicitHeight: 38
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    font.pixelSize: 14
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: "#d4d4d8"
    selectedTextColor: Theme.text
    selectByMouse: true
    opacity: enabled ? 1 : 0.5

    background: Rectangle {
        color: root.enabled ? Theme.background : Theme.surface
        radius: Theme.radius
        border.width: root.activeFocus ? 2 : 1
        border.color: root.activeFocus ? Theme.ring : Theme.border
    }
}
