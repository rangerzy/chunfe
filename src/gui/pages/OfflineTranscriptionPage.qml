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
    readonly property bool dirty: controller && modelPath.text.trim() !== controller.modelPath
    readonly property bool busy: controller && (controller.engineStatus === "starting" || controller.engineStatus === "stopping")

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("离线转写配置")
        subtitle: qsTr("转写引擎已内置，选择模型文件后保存并加载。")
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
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("模型文件")
            description: qsTr("选择兼容的 Qwen3-ASR GGUF 模型。")
            RowLayout {
                Layout.fillWidth: true
                AppField {
                    id: modelPath
                    objectName: "offlineModelField"
                    Layout.fillWidth: true
                    text: root.controller ? root.controller.modelPath : ""
                    placeholderText: qsTr("输入模型文件的绝对路径")
                    Accessible.name: qsTr("模型文件路径")
                }
                AppButton { text: qsTr("选择文件"); variant: "secondary"; onClicked: modelDialog.open() }
            }
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("引擎状态")
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                StatusBadge {
                    text: !root.controller ? qsTr("服务不可用") : root.controller.engineStatus === "ready" ? qsTr("已就绪") : root.controller.engineStatus === "starting" ? qsTr("正在加载") : root.controller.engineStatus === "stopping" ? qsTr("正在卸载") : root.controller.engineStatus === "error" ? qsTr("加载失败") : qsTr("未加载")
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.controller && root.controller.engineError !== ""
                    text: root.controller ? root.controller.engineError : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    visible: !!root.dirty
                    text: qsTr("模型路径已更改，请先保存；已加载的引擎使用原模型。")
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: 12
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "offlineSaveButton"
                text: qsTr("保存配置")
                variant: "secondary"
                enabled: !!root.controller && !root.busy
                onClicked: root.controller.saveModelPath(modelPath.text.trim())
            }
            AppButton {
                objectName: "offlineLoadButton"
                text: root.controller && root.controller.engineStatus === "ready" ? qsTr("卸载引擎") : root.busy ? qsTr("正在处理…") : qsTr("加载引擎")
                enabled: !!root.controller && !root.busy && (root.controller.engineStatus === "ready" || (!root.dirty && modelPath.text.trim() !== ""))
                onClicked: {
                    if (root.controller.engineStatus === "ready") root.controller.unloadEngine()
                    else root.controller.loadEngine()
                }
            }
        }
    }
}
