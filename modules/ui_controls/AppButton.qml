import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

Button {
    id: root

    property string variant: "primary"
    property string iconName: ""
    property real cornerRadius: Theme.radius
    readonly property color foreground: variant === "primary" || variant === "danger" ? Theme.primaryText : Theme.text

    implicitWidth: Math.max(72, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 36
    leftPadding: 14
    rightPadding: 14
    topPadding: 8
    bottomPadding: 8
    spacing: 8
    font.pixelSize: 14
    font.weight: Font.Medium
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.45

    contentItem: RowLayout {
        spacing: root.spacing
        AppIcon {
            visible: root.iconName !== ""
            name: root.iconName
            color: root.foreground
            size: 16
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: root.text
            font: root.font
            color: root.foreground
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.fillWidth: true
        }
    }

    background: Rectangle {
        radius: root.cornerRadius
        color: root.variant === "danger" ? (root.down ? "#991b1b" : root.hovered ? "#dc2626" : Theme.danger) : root.variant === "primary" ? (root.down ? "#09090b" : root.hovered ? "#3f3f46" : Theme.primary) : root.down ? "#e4e4e7" : root.hovered ? Theme.hover : root.variant === "ghost" ? "transparent" : Theme.background
        border.width: root.visualFocus ? 2 : root.variant === "secondary" ? 1 : 0
        border.color: root.visualFocus ? Theme.ring : Theme.border
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
}
