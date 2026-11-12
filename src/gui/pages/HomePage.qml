pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
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
            text: qsTr("实时转写与回应建议")
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
                model: [
                    {
                        icon: "model",
                        label: qsTr("模型"),
                        value: root.controller.modelName || qsTr("未配置模型"),
                        route: "model"
                    },
                    {
                        icon: "audio",
                        label: qsTr("实时转写"),
                        value: root.controller.engineStatus === "ready" ? qsTr("转写就绪") : root.controller.engineStatus === "starting" ? qsTr("转写启动中") : qsTr("转写未启动"),
                        route: "audio"
                    },
                    {
                        icon: "file",
                        label: qsTr("背景资料"),
                        value: root.controller.backgroundText ? qsTr("背景已配置") : qsTr("未设置背景"),
                        route: "background"
                    }
                ]
                delegate: Button {
                    id: statusButton
                    required property var modelData
                    implicitWidth: Math.min(154, implicitContentWidth + 20)
                    implicitHeight: 32
                    leftPadding: 10
                    rightPadding: 10
                    hoverEnabled: true
                    Accessible.name: modelData.label + "，" + modelData.value
                    onClicked: root.controller.route = modelData.route
                    ToolTip.visible: hovered
                    ToolTip.delay: 650
                    ToolTip.text: modelData.label + " · " + modelData.value
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
