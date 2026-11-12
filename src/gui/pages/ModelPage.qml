pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    maximumContentWidth: 640
    property bool keyVisible: false

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("模型连接")
        subtitle: qsTr("配置模型接口，之后的对话会自动沿用。")
    }
    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 20
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("API 地址")
            description: qsTr("填写服务商提供的基础地址，包含版本路径。")
            AppField {
                id: endpoint
                objectName: "endpointField"
                Layout.fillWidth: true
                text: root.controller.apiEndpoint
                placeholderText: "https://api.example.com/v1"
                selectByMouse: true
                Accessible.name: qsTr("API 地址")
            }
        }
        SettingRow {
            Layout.fillWidth: true
            label: "API Key"
            AppField {
                id: key
                objectName: "apiKeyField"
                Layout.fillWidth: true
                rightPadding: 44
                text: root.controller.apiKey
                placeholderText: qsTr("输入 API Key")
                echoMode: root.keyVisible ? TextInput.Normal : TextInput.Password
                selectByMouse: true
                Accessible.name: "API Key"
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 3
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "eye"
                    text: root.keyVisible ? qsTr("隐藏 Key") : qsTr("显示 Key")
                    onClicked: root.keyVisible = !root.keyVisible
                }
            }
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("模型名称")
            AppField {
                id: model
                objectName: "modelNameField"
                Layout.fillWidth: true
                text: root.controller.modelName
                placeholderText: qsTr("例如：你的模型 ID")
                selectByMouse: true
                Accessible.name: qsTr("模型名称")
            }
        }
        SettingRow {
            Layout.fillWidth: true
            visible: root.controller.connectionStatus !== "idle"
            label: qsTr("连接状态")
            minimumHeight: 24
            Label {
                Layout.fillWidth: true
                text: root.controller.connectionStatus === "testing" ? qsTr("正在测试连接…") : root.controller.connectionStatus === "connected" ? qsTr("连接成功") : qsTr("连接失败，请检查配置后重试。")
                color: Theme.text
                wrapMode: Text.Wrap
                font.pixelSize: 12
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "testConnectionButton"
                variant: "secondary"
                text: root.controller.connectionStatus === "testing" ? qsTr("正在测试…") : qsTr("测试连接")
                enabled: root.controller.connectionStatus !== "testing" && endpoint.text.trim().length > 0 && model.text.trim().length > 0
                onClicked: {
                    root.controller.saveModel(endpoint.text, key.text, model.text);
                    root.controller.testConnection();
                }
            }
            AppButton {
                objectName: "saveModelButton"
                text: qsTr("保存配置")
                onClicked: root.controller.saveModel(endpoint.text, key.text, model.text)
            }
        }
    }
}
