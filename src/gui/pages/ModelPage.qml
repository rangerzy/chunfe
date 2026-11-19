pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    property bool testChatAvailable: false
    signal testChatRequested()
    maximumContentWidth: 640
    property bool keyVisible: false
    readonly property bool configurationDirty: controller?.configurationDirty ?? false
    readonly property string verificationStatus: controller?.verificationStatus ?? "unconfigured"

    function saveChanges(): bool {
        return !configurationDirty || controller.saveModel();
    }

    function verificationLabel(): string {
        if (configurationDirty)
            return qsTr("等待保存");
        const restored = controller?.verificationRestored ?? false;
        switch (verificationStatus) {
        case "untested": return qsTr("待检测");
        case "testing": return qsTr("检测中…");
        case "passed": return restored ? qsTr("上次检测通过") : qsTr("API 连接正常");
        case "failed": {
            const message = controller.verificationMessage;
            if (restored)
                return message ? qsTr("上次检测失败。%1").arg(message) : qsTr("上次检测失败");
            return message || qsTr("检测失败");
        }
        default: return qsTr("未配置 API Key");
        }
    }

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("模型连接")
        subtitle: qsTr("配置 DeepSeek API，并查看连接状态。")
    }
    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 20
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("API 地址")
            AppField {
                objectName: "endpointField"
                Layout.fillWidth: true
                text: root.controller ? root.controller.apiEndpoint : ""
                readOnly: true
                selectByMouse: true
                Accessible.name: qsTr("API 地址")
            }
        }
        SettingRow {
            id: keyRow
            Layout.fillWidth: true
            label: "API Key"
            description: qsTr("结束编辑后自动保存到 chunfeng/config.ini，并检测新 Key。")
            AppField {
                objectName: "apiKeyField"
                property bool edited: false
                Layout.fillWidth: true
                rightPadding: 44
                text: root.controller ? root.controller.draftApiKey : ""
                enabled: root.controller !== null
                placeholderText: qsTr("输入 API Key")
                echoMode: root.keyVisible ? TextInput.Normal : TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                selectByMouse: true
                Accessible.name: "API Key"
                Accessible.description: keyRow.description
                onTextEdited: {
                    edited = true;
                    root.controller.draftApiKey = text;
                }
                onEditingFinished: {
                    if (edited && root.saveChanges())
                        edited = false;
                }
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
            SelectField {
                objectName: "modelNameField"
                Layout.fillWidth: true
                enabled: root.controller !== null
                model: root.controller ? root.controller.availableModels : []
                currentIndex: root.controller ? root.controller.availableModels.indexOf(root.controller.draftModelName) : -1
                Accessible.name: qsTr("模型名称")
                onActivated: index => {
                    root.controller.draftModelName = textAt(index);
                    root.saveChanges();
                }
            }
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("连接状态")
            minimumHeight: 24
            Label {
                objectName: "modelVerificationStatus"
                Layout.fillWidth: true
                text: root.verificationLabel()
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: root.configurationDirty ? Theme.muted
                    : root.verificationStatus === "failed" ? Theme.danger : Theme.text
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
                text: qsTr("测试连接")
                focusPolicy: Qt.TabFocus
                enabled: root.controller !== null && root.controller.draftApiKey.trim().length > 0
                         && (root.verificationStatus !== "testing" || root.configurationDirty)
                onClicked: {
                    if (root.saveChanges() && root.verificationStatus !== "testing")
                        root.controller.testConnection();
                }
            }
            AppButton {
                objectName: "testChatButton"
                variant: "secondary"
                text: qsTr("测试对话")
                focusPolicy: Qt.TabFocus
                enabled: root.testChatAvailable && root.controller !== null
                         && root.controller.draftApiKey.trim().length > 0
                         && (root.controller.modelConfigured || root.configurationDirty)
                onClicked: {
                    if (root.saveChanges() && root.controller.modelConfigured)
                        root.testChatRequested();
                }
            }
        }
    }
}
