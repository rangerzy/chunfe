import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

Item {
    id: root

    property string channel: "SPK"
    property string channelName: qsTr("系统声音")
    property string deviceName: ""
    property string inputState: "waiting"
    property real level: 0

    implicitWidth: 580
    implicitHeight: 46
    Accessible.role: Accessible.Grouping
    Accessible.name: channel + "，" + channelName + "，" + deviceName + (inputState === "error" ? "，" + qsTr("设备异常") : "")

    RowLayout {
        anchors.fill: parent
        spacing: 12

        AppIcon {
            name: root.channel === "MIC" ? "mic" : "headphones"
            size: 20
            color: root.inputState === "error" ? Theme.danger : Theme.muted
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3
            Text {
                objectName: "audioTestChannelLabel"
                Layout.fillWidth: true
                text: root.channel
                textFormat: Text.PlainText
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.deviceName || qsTr("未选择设备")
                textFormat: Text.PlainText
                color: Theme.muted
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
        }
        ProgressBar {
            objectName: root.channel.toLowerCase() + "TestLevel"
            Layout.preferredWidth: 160
            Layout.preferredHeight: 6
            from: 0
            to: 1
            value: Math.max(0, Math.min(1, root.level))
            Accessible.name: root.channel + qsTr(" 音量")
            background: Rectangle {
                implicitHeight: 6
                radius: 3
                color: Theme.hover
            }
            contentItem: Item {
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, root.level))
                    height: parent.height
                    radius: 3
                    color: root.inputState === "error" ? Theme.danger : Theme.text
                    Behavior on width {
                        NumberAnimation {
                            duration: 100
                        }
                    }
                }
            }
        }
    }
}
