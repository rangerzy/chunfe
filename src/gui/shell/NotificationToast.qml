import QtQuick
import QtQuick.Layouts
import Chunfe.Ui

Rectangle {
    id: root

    property string message: ""
    property string severity: "info"
    property real maximumWidth: 480

    visible: message.length > 0
    width: Math.min(implicitWidth, maximumWidth)
    height: implicitHeight
    implicitWidth: icon.implicitWidth + row.spacing + label.implicitWidth + 28
    implicitHeight: Math.max(42, row.implicitHeight + 20)
    radius: Theme.radius
    color: Theme.darkBackground
    border.color: Theme.darkBorder

    Accessible.role: Accessible.StaticText
    Accessible.name: message

    function showNotification(text: string, kind: string): void {
        if (!text.trim())
            return;

        const wasVisible = visible;
        severity = kind;
        message = text;
        dismissTimer.restart();
        if (!wasVisible)
            fadeIn.restart();
        Accessible.announce(text);
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 9

        AppIcon {
            id: icon
            size: 16
            name: root.severity === "success" ? "check" : root.severity === "error" ? "alert" : "info"
            color: root.severity === "success" ? "#a7f3d0" : root.severity === "error" ? "#fca5a5" : Theme.darkMuted
        }

        Text {
            id: label
            Layout.fillWidth: true
            text: root.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: 13
            Accessible.ignored: true
        }
    }

    NumberAnimation {
        id: fadeIn
        target: root
        property: "opacity"
        from: 0
        to: 1
        duration: 120
    }

    Timer {
        id: dismissTimer
        interval: root.severity === "error" ? 6000 : 4000
        onTriggered: root.message = ""
    }
}
