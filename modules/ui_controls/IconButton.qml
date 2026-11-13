import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui

Button {
    id: root

    property string iconName: ""
    property bool dark: false

    implicitWidth: 32
    implicitHeight: 32
    padding: 7
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.4
    Accessible.name: text

    contentItem: AppIcon {
        name: root.iconName
        color: root.dark ? Theme.darkText : Theme.text
        size: 18
    }

    background: Rectangle {
        radius: 6
        color: root.down ? (root.dark ? "#52525b" : "#e4e4e7")
             : root.hovered ? (root.dark ? Theme.darkBorder : Theme.hover) : "transparent"
        border.width: root.visualFocus ? 1 : 0
        border.color: Theme.ring
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    AppToolTip {
        trigger: root
        text: root.text
    }
}
