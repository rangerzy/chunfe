pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    objectName: "offlineTranscriptionPage"
    property var controller: null
    maximumContentWidth: 640
    readonly property bool loaded: controller && controller.engineStatus === "ready"
    readonly property bool busy: controller && (controller.engineStatus === "starting" || controller.engineStatus === "stopping")
    Component.onCompleted: modelPath.text = controller ? controller.attemptedModelPath || controller.modelPath : ""

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("离线转写配置")
        subtitle: qsTr("加载成功后记住模型路径，每次启动需手动加载。")
    }
    FileDialog {
        id: modelDialog
        title: qsTr("选择转写模型文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GGUF 模型 (*.gguf)"), qsTr("所有文件 (*)")]
        onAccepted: if (root.controller) modelPath.text = root.controller.pathFromUrl(selectedFile)
    }
    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 20
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            SettingRow {
                id: modelRow
                Layout.fillWidth: true
                label: qsTr("模型文件")
                description: modelResult.visible ? "" : qsTr("选择兼容的 Qwen3-ASR GGUF 模型。")
                RowLayout {
                    Layout.fillWidth: true
                    AppField {
                        id: modelPath
                        objectName: "offlineModelField"
                        Layout.fillWidth: true
                        readOnly: root.loaded || root.busy
                        placeholderText: qsTr("输入模型文件的绝对路径")
                        Accessible.name: qsTr("模型文件路径")
                    }
                    AppButton {
                        objectName: "offlineModelPicker"
                        text: qsTr("选择文件")
                        variant: "secondary"
                        enabled: !!root.controller && !root.loaded && !root.busy
                        onClicked: modelDialog.open()
                    }
                }
            }
            Text {
                id: modelResult
                objectName: "offlineModelResult"
                Layout.fillWidth: true
                Layout.leftMargin: modelRow.labelWidth + modelRow.spacing
                visible: !root.busy && text !== ""
                text: !root.controller ? qsTr("服务不可用")
                    : root.controller.engineError !== "" ? root.controller.engineError
                    : root.loaded && modelPath.text.trim() === root.controller.modelPath ? qsTr("已加载") : ""
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: !root.controller || root.controller.engineError !== "" ? Theme.danger : Theme.muted
                font.pixelSize: 12
                onTextChanged: if (text !== "" && !root.busy) Accessible.announce(text)
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "offlineLoadButton"
                variant: root.busy ? "secondary" : "primary"
                opacity: enabled || root.busy ? 1 : 0.45
                text: root.loaded ? qsTr("卸载模型")
                    : root.controller && root.controller.engineStatus === "starting" ? qsTr("正在加载…")
                    : root.controller && root.controller.engineStatus === "stopping" ? qsTr("正在卸载…") : qsTr("加载模型")
                enabled: !!root.controller && !root.busy && (root.loaded || modelPath.text.trim() !== "")
                onClicked: {
                    if (root.loaded) root.controller.unloadEngine()
                    else {
                        modelPath.text = modelPath.text.trim()
                        root.controller.loadEngine(modelPath.text)
                    }
                }
            }
        }
    }
}
