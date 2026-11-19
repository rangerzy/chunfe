import QtQuick
import Chunfe.Ui

Rectangle {
    id: root

    property string text: ""
    property string tone: "neutral"
    property bool dark: false

    implicitWidth: label.implicitWidth + 18
    implicitHeight: 24
    radius: 5
    color: tone === "danger" ? (dark ? "#451a1a" : "#fef2f2")
         : tone === "warning" ? (dark ? "#422006" : "#fffbeb")
         : dark ? Theme.darkSurface : Theme.hover
    border.width: 1
    border.color: dark ? Theme.darkBorder : Theme.border

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 12
        font.weight: Font.Medium
        color: root.tone === "danger" ? (root.dark ? "#fca5a5" : Theme.danger)
             : root.tone === "warning" ? (root.dark ? "#fcd34d" : "#92400e")
             : root.dark ? Theme.darkText : Theme.text
    }
}
