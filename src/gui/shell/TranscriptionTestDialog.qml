pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui
import Chunfe.AudioTest

Dialog {
    id: root
    required property var controller
    objectName: "transcriptionTestDialog"
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(480, parent ? parent.height - 40 : 480)
    padding: 20
    topPadding: 0
    spacing: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    title: qsTr("测试转写")
    onAboutToHide: {
        if (controller.opened)
            controller.close();
    }
    background: Rectangle {
        radius: 16
        color: Theme.background
        border.color: Theme.border
    }
    header: Item {
        implicitHeight: 56
        Text {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        IconButton {
            objectName: "closeTranscriptionTestButton"
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            iconName: "close"
            text: qsTr("关闭转写测试")
            onClicked: root.controller.close()
        }
    }
    contentItem: TranscriptionTestPanel {
        objectName: "transcriptionTestPanel"
        status: root.controller.status
        elapsedText: root.controller.elapsedText
        spkDevice: root.controller.spkDevice
        micDevice: root.controller.micDevice
        spkState: root.controller.spkState
        micState: root.controller.micState
        spkLevel: root.controller.spkLevel
        micLevel: root.controller.micLevel
        lines: root.controller.lines
        errorMessage: root.controller.errorMessage
        onStopRequested: root.controller.stop()
        onRestartRequested: root.controller.restart()
    }
    Connections {
        target: root.controller
        function onOpenedChanged() {
            if (root.controller.opened)
                root.open();
            else
                root.close();
        }
    }
}
