pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    property string modelVerificationStatus: "unconfigured"
    property bool modelVerificationRestored: false
    property string offlineEngineStatus: "stopped"
    readonly property string modelStatusLabel: {
        switch (modelVerificationStatus) {
        case "passed": return modelVerificationRestored ? qsTr("上次检测通过") : qsTr("API 连接正常")
        case "failed": return modelVerificationRestored ? qsTr("上次检测失败") : qsTr("模型检测失败")
        case "testing": return qsTr("模型检测中")
        case "untested": return qsTr("模型待检测")
        default: return qsTr("模型未配置")
        }
    }
    maximumContentWidth: 480
    pagePadding: 32
    centerContent: true

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 56
            implicitHeight: 56
            radius: 16
            color: Theme.surface
            border.color: Theme.border
            AppIcon {
                anchors.centerIn: parent
                name: "mic"
                size: 26
                color: Theme.text
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 20
            text: qsTr("开始一段对话")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.text
            font.pixelSize: 28
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            text: root.controller.isDemo ? qsTr("实时转写与回应建议演示") : qsTr("使用离线模型实时转写系统声音和麦克风")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            font.pixelSize: 14
        }
        AppButton {
            id: startButton
            objectName: "startInterviewButton"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 28
            implicitWidth: 180
            implicitHeight: 44
            cornerRadius: 12
            text: qsTr("开始对话")
            onClicked: root.controller.startChecks()
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: false
            Layout.maximumWidth: root.bodyWidth
            Layout.topMargin: 18
            spacing: 4
            Repeater {
                objectName: "homeStatusItems"
                model: [
                    {
                        icon: "model",
                        label: qsTr("模型"),
                        action: qsTr("配置模型"),
                        value: root.modelStatusLabel,
                        route: "model"
                    },
                    {
                        icon: "audio",
                        label: qsTr("实时转写"),
                        action: qsTr("打开音频与转写设置"),
                        value: root.offlineEngineStatus === "ready" ? qsTr("转写就绪")
                               : root.offlineEngineStatus === "starting" ? qsTr("转写加载中") : qsTr("转写未就绪"),
                        route: "audio"
                    },
                    {
                        icon: "file",
                        label: qsTr("背景资料"),
                        action: qsTr("编辑背景资料"),
                        value: root.controller.backgroundText.trim().length > 0 ? qsTr("背景已配置") : qsTr("未设置背景"),
                        route: "background"
                    }
                ]
                delegate: Button {
                    id: statusButton
                    required property var modelData
                    objectName: modelData.route + "HomeStatus"
                    implicitWidth: Math.min(154, implicitContentWidth + 20)
                    implicitHeight: 32
                    leftPadding: 10
                    rightPadding: 10
                    hoverEnabled: true
                    Accessible.name: modelData.label + "，" + modelData.value
                    Accessible.description: modelData.action
                    onClicked: root.controller.route = modelData.route
                    AppToolTip {
                        trigger: statusButton
                        text: statusButton.modelData.action
                              + (statusValue.truncated ? "\n" + statusButton.modelData.value : "")
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: statusButton.hovered ? Theme.hover : "transparent"
                        border.width: statusButton.visualFocus ? 2 : 0
                        border.color: Theme.ring
                        Behavior on color {
                            ColorAnimation {
                                duration: 120
                            }
                        }
                    }
                    contentItem: RowLayout {
                        spacing: 6
                        AppIcon {
                            name: statusButton.modelData.icon
                            size: 16
                            color: Theme.muted
                        }
                        Text {
                            id: statusValue
                            Layout.fillWidth: true
                            text: statusButton.modelData.value
                            font.pixelSize: 12
                            color: Theme.muted
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
