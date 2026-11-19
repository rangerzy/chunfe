pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui

Dialog {
    id: root
    required property var controller
    width: 460
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    title: controller.isDemo ? qsTr("正在准备演示对话") : qsTr("准备实时转写")
    onRejected: controller.cancelChecks()
    background: Rectangle { radius: 10; color: Theme.background; border.color: Theme.border }
    header: Item {
        implicitHeight: 60
        Text { x: 20; y: 20; text: root.title; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
        IconButton { anchors.right: parent.right; anchors.rightMargin: 14; y: 14; iconName: "close"; text: qsTr("取消检查"); onClicked: root.controller.cancelChecks() }
    }
    contentItem: Column {
        spacing: 12
        Text {
            width: parent.width
            visible: !root.controller.isDemo
            objectName: "nativePreflightMessage"
            text: root.controller.nativeCheckMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.pixelSize: 14
            color: root.controller.checkState === "failed" ? Theme.danger : Theme.text
        }
        Text { visible: root.controller.isDemo; text: qsTr("检查完成后，将自动进入悬浮辅助。"); font.pixelSize: 14; color: Theme.muted }
        ProgressBar {
            visible: root.controller.isDemo
            width: parent.width; from: 0; to: 5; value: Math.min(root.controller.checkStep, 5)
            background: Rectangle { implicitHeight: 4; color: "#f4f4f5"; radius: 2 }
            contentItem: Item { implicitHeight: 4; Rectangle { width: parent.width * Math.min(root.controller.checkStep / 5, 1); height: 4; radius: 2; color: Theme.text; Behavior on width { NumberAnimation { duration: 180 } } } }
        }
        Repeater {
            model: root.controller.isDemo
                   ? [qsTr("离线转写引擎"), qsTr("系统音频"), qsTr("麦克风"), qsTr("模型连接"), qsTr("背景资料")] : []
            delegate: Item {
                id: row
                required property string modelData
                required property int index
                readonly property bool complete: root.controller.checkStep > index
                readonly property bool failed: complete && ((index === 0 && root.controller.startScenario === "engine") || (index === 3 && root.controller.startScenario === "model"))
                width: parent.width; height: 40
                Rectangle { width: 34; height: 34; y: 3; radius: 6; color: Theme.surface; border.color: Theme.border
                    AppIcon { anchors.centerIn: parent; name: ["audio","headphones","mic","model","file"][row.index]; size: 17; color: Theme.muted } }
                Column {
                    x: 48; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Text { text: row.modelData; font.pixelSize: 14; color: Theme.text }
                    Text { text: row.index === 4 ? (root.controller.backgroundText ? qsTr("自动使用已保存的背景") : qsTr("未设置，不影响开始")) : [qsTr("本地引擎"), qsTr("接收对方声音"), qsTr("接收你的声音"), root.controller.modelName, ""][row.index]; font.pixelSize: 12; color: Theme.muted }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: row.failed ? qsTr("需处理") : row.complete ? qsTr("已就绪") : root.controller.checkStep === row.index ? qsTr("检查中…") : qsTr("等待")
                    color: row.failed ? Theme.danger : row.complete ? Theme.text : Theme.muted
                    font.pixelSize: 12
                }
            }
        }
        Text {
            width: parent.width; visible: root.controller.isDemo && root.controller.checkState === "failed"
            text: root.controller.startScenario === "engine" ? qsTr("引擎启动失败，请重试检查。") : qsTr("演示模型检查失败，请重试。")
            color: Theme.danger; font.pixelSize: 12; wrapMode: Text.Wrap
        }
        Row {
            anchors.right: parent.right; spacing: 8
            AppButton { variant: "secondary"; text: qsTr("取消"); onClicked: root.controller.cancelChecks() }
            AppButton {
                objectName: "loadConversationEngineButton"
                visible: root.controller.checkState === "failed" && root.controller.canLoadConversationEngine
                text: qsTr("前往加载模型")
                onClicked: root.controller.openConversationEngineSettings()
            }
            AppButton {
                visible: root.controller.checkState === "failed" && !root.controller.canLoadConversationEngine
                text: qsTr("重试检查")
                onClicked: root.controller.retryChecks()
            }
        }
    }
}
