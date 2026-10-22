import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root
    property bool primary: false
    property color foregroundColor: "#24352d"
    property color accentColor: "#35634a"

    implicitHeight: 36
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 13
    contentItem: Text {
        text: root.text
        font: root.font
        color: root.primary ? "#ffffff" : root.foregroundColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        color: root.primary
            ? (root.down ? "#284c38" : root.hovered ? "#427555" : root.accentColor)
            : (root.down ? "#dfe7db" : root.hovered ? "#edf2e9" : "#f5f7f2")
        border.color: root.activeFocus ? root.accentColor : "transparent"
    }
}
