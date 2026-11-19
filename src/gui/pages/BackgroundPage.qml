pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    required property var analysisController
    maximumContentWidth: 640
    readonly property bool hasDocument: (controller?.backgroundText ?? "").trim().length > 0
    readonly property bool analyzing: analysisController?.running ?? false
    readonly property string analysisText: analysisController?.resultText ?? ""
    readonly property string analysisMessage: analysisController?.statusMessage ?? ""
    readonly property bool hasAnalysis: analyzing || analysisText.length > 0 || analysisMessage.length > 0

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("背景资料")
        subtitle: qsTr("导入的 Markdown 保存在本机，点击「AI 分析」会将全文发送给 DeepSeek。")
    }
    FileDialog {
        id: fileDialog
        objectName: "backgroundFileDialog"
        title: qsTr("选择背景资料")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Markdown 文件 (*.md *.markdown *.MD *.MARKDOWN)")]
        onAccepted: {
            if (!root.analyzing)
                root.controller?.importMarkdown(selectedFile);
        }
    }
    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: root.hasDocument && root.analysisController && !root.analysisController.configured
                    ? qsTr("在「模型连接」配置 API Key 后可分析。") : ""
                wrapMode: Text.Wrap
                color: Theme.muted
                font.pixelSize: 12
            }
            AppButton {
                objectName: "backgroundImportButton"
                text: qsTr("导入 Markdown")
                enabled: root.controller !== null && !root.analyzing
                onClicked: fileDialog.open()
            }
            AppButton {
                objectName: "backgroundAnalysisButton"
                text: root.analyzing ? qsTr("停止分析") : qsTr("AI 分析")
                variant: "secondary"
                enabled: root.analyzing || (root.hasDocument && (root.analysisController?.configured ?? false))
                onClicked: {
                    if (root.analyzing)
                        root.analysisController.cancelAnalysis();
                    else
                        root.analysisController.startAnalysis();
                }
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: root.hasAnalysis ? 130 : 270
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            background: Rectangle {
                color: Theme.background
                border.color: preview.activeFocus ? Theme.ring : Theme.border
                radius: Theme.radius
            }
            TextArea {
                id: preview
                objectName: "backgroundPreview"
                Accessible.name: qsTr("背景资料预览")
                text: root.controller?.backgroundText ?? ""
                placeholderText: qsTr("尚未导入背景资料。")
                placeholderTextColor: Theme.muted
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: true
                font.pixelSize: 14
                color: Theme.text
                padding: 14
                background: null
            }
        }
        Label {
            id: importError
            objectName: "backgroundImportError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.controller?.errorMessage ?? ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: 12
            onTextChanged: {
                if (importError.text.length > 0)
                    Accessible.announce(importError.text);
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.hasAnalysis

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: qsTr("分析结果")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Label {
                    id: statusLabel
                    objectName: "backgroundAnalysisStatus"
                    Layout.fillWidth: true
                    text: root.analysisMessage || (root.analyzing ? qsTr("正在分析…") : "")
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: root.analysisController?.failed ? Theme.danger : Theme.muted
                    font.pixelSize: 12
                    onTextChanged: {
                        if (statusLabel.text.length > 0)
                            Accessible.announce(statusLabel.text);
                    }
                }
            }
            ScrollView {
                id: analysisScroll
                objectName: "backgroundAnalysisScroll"
                readonly property Flickable viewport: contentItem as Flickable
                property real previousContentHeight: 0
                property real previousViewportHeight: 0
                Layout.fillWidth: true
                Layout.preferredHeight: 145
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                visible: root.analyzing || result.length > 0

                function scheduleFollow() {
                    const previousEnd = viewport.originY + Math.max(0, previousContentHeight - previousViewportHeight);
                    previousContentHeight = viewport.contentHeight;
                    previousViewportHeight = viewport.height;
                    if (follow.running || viewport.contentY < previousEnd - 1)
                        return;
                    follow.previousPosition = viewport.contentY;
                    follow.start();
                }

                Timer {
                    id: follow
                    interval: 0
                    property real previousPosition: 0
                    onTriggered: {
                        const view = analysisScroll.viewport;
                        if (view && !view.moving && result.selectionStart === result.selectionEnd
                                && Math.abs(view.contentY - previousPosition) <= 1) {
                            view.contentY = view.originY + Math.max(0, view.contentHeight - view.height);
                        }
                    }
                }
                Connections {
                    target: analysisScroll.viewport
                    function onContentHeightChanged() { analysisScroll.scheduleFollow(); }
                    function onHeightChanged() { analysisScroll.scheduleFollow(); }
                }
                background: Rectangle {
                    color: Theme.background
                    border.color: result.activeFocus ? Theme.ring : Theme.border
                    radius: Theme.radius
                }
                TextArea {
                    id: result
                    objectName: "backgroundAnalysisResult"
                    readonly property string incomingText: root.analysisText
                    Accessible.name: qsTr("分析结果")
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: 14
                    color: Theme.text
                    padding: 14
                    background: null

                    function updateText() {
                        if (incomingText === text)
                            return;
                        if (incomingText.startsWith(text))
                            insert(length, incomingText.slice(length));
                        else
                            text = incomingText;
                    }

                    Component.onCompleted: updateText()
                    onIncomingTextChanged: updateText()
                }
            }
        }
    }
}
