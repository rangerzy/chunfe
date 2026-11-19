import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui

Button {
    id: root
    property string iconName: "home"
    property bool selected: false
    property bool keyboardExpansionAllowed: true
    readonly property bool expanded: hovered || (visualFocus && keyboardExpansionAllowed)
    readonly property color foreground: selected ? Theme.primaryText : Theme.text
    z: expanded ? 2 : 1
    width: expanded ? 164 : 44
    height: 40
    hoverEnabled: true
    Accessible.name: text
    background: Rectangle {
        radius: 6
        color: root.selected ? (root.down ? "#09090b" : root.hovered ? "#27272a" : Theme.primary)
                             : root.down ? "#d4d4d8" : root.hovered ? "#e4e4e7" : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: root.selected ? "#a1a1aa" : Theme.text
        Behavior on color { ColorAnimation { duration: 120 } }
    }
    contentItem: Item {
        AppIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: root.iconName; size: 20; color: root.foreground }
        Text {
            x: 44
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.foreground
            font.pixelSize: 14
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            opacity: root.expanded ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 160 } }
        }
    }
    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
}
