pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    readonly property var record: controller.selectedRecord
    property int activeTab: root.record?.isDemo === false && !root.record.questions.length ? 1 : 0

    AppButton {
        variant: "ghost"
        text: qsTr("全部对话")
        iconName: "chevron-left"
        onClicked: root.controller.route = "history"
    }
    Label {
        Layout.fillWidth: true
        visible: root.record?.unsaved === true
        text: qsTr("这条记录当前仅保存在内存中。")
        color: Theme.danger
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: !!root.record
        spacing: 16
        PageHeader {
            Layout.fillWidth: true
            title: root.record ? root.record.title : ""
            subtitle: root.record ? String(root.record.date || "") + " · " + String(root.record.duration || "") + " · " + String(root.record.model || "") : ""
        }
        AppButton {
            text: qsTr("重试保存")
            variant: "secondary"
            visible: root.record?.unsaved === true
            onClicked: root.controller.retryRecordSave(root.record.id)
        }
        AppButton {
            text: qsTr("重命名")
            variant: "secondary"
            onClicked: {
                renameInput.text = root.record.title;
                renameDialog.open();
            }
        }
        AppButton {
            text: qsTr("导出 Markdown")
            iconName: "download"
            visible: root.record?.isDemo === true
            onClicked: root.controller.exportRecord(root.record.id)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: !!root.record
        spacing: 8
        Repeater {
            model: [qsTr("回应建议"), qsTr("完整转写"), qsTr("背景快照")]
            delegate: AppButton {
                id: tabButton
                required property int index
                required property string modelData
                text: tabButton.modelData
                visible: tabButton.index !== 0 || root.record?.isDemo !== false || root.record.questions.length > 0
                variant: root.activeTab === tabButton.index ? "secondary" : "ghost"
                onClicked: root.activeTab = tabButton.index
            }
        }
        Item {
            Layout.fillWidth: true
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.border
        visible: !!root.record
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !!root.record && root.activeTab === 0
        spacing: 18
        Repeater {
            model: root.record ? root.record.questions : []
            delegate: FormCard {
                id: questionCard
                required property int index
                required property var modelData
                Layout.fillWidth: true
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    text: qsTr("建议 %1").arg(String(questionCard.index + 1).padStart(2, "0")) + (questionCard.modelData.kind === "request" ? qsTr(" · 请求") : questionCard.modelData.kind === "objection" ? qsTr(" · 异议") : qsTr(" · 问题"))
                          + (questionCard.modelData.time ? " · " + questionCard.modelData.time : "")
                          + (questionCard.modelData.incomplete === true ? qsTr(" · 未完成") : "")
                    textFormat: Text.PlainText
                    color: Theme.muted
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
                Label {
                    Layout.fillWidth: true
                    text: questionCard.modelData.question
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    lineHeight: 1.4
                }
                TextArea {
                    Layout.fillWidth: true
                    implicitWidth: 0
                    text: questionCard.modelData.answer || qsTr("这段对话尚未生成建议。")
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    color: Theme.text
                    font.pixelSize: 15
                    padding: 0
                    background: null
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: !root.record || !root.record.questions.length
            text: qsTr("本次暂无回应建议，可查看完整转写。")
            color: Theme.muted
            font.pixelSize: 15
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !!root.record && root.activeTab === 1
        spacing: 16
        Repeater {
            model: root.record ? root.record.transcript : []
            delegate: Item {
                id: message
                required property var modelData
                readonly property bool isMic: modelData.source === "mic" || (!modelData.source && modelData.role === "me")
                Layout.fillWidth: true
                implicitHeight: bubble.implicitHeight
                Rectangle {
                    id: bubble
                    width: Math.min(message.width * 0.84, 640)
                    x: message.isMic ? message.width - width : 0
                    implicitHeight: messageContent.implicitHeight + 28
                    radius: 8
                    color: message.isMic ? "#f4f4f5" : Theme.background
                    border.color: Theme.border
                    ColumnLayout {
                        id: messageContent
                        x: 14
                        y: 14
                        width: parent.width - 28
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: message.modelData.source === "mic" ? "MIC"
                                      : message.modelData.source === "spk" ? "SPK"
                                      : message.isMic ? qsTr("我") : qsTr("对方")
                                color: Theme.muted
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Label {
                                text: String(message.modelData.time || "")
                                color: Theme.muted
                                font.pixelSize: 12
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: message.modelData.text + (message.modelData.incomplete === true ? qsTr("（未完成）") : "")
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: 15
                            wrapMode: Text.Wrap
                            lineHeight: 1.4
                        }
                    }
                }
            }
        }
        Label {
            visible: !root.record || !root.record.transcript.length
            text: qsTr("本次暂无转写内容。")
            color: Theme.muted
            font.pixelSize: 15
        }
    }

    FormCard {
        Layout.fillWidth: true
        visible: !!root.record && root.activeTab === 2
        Label {
            text: qsTr("本次使用的背景")
            font.pixelSize: 14
            font.weight: Font.Medium
            color: Theme.text
        }
        TextArea {
            Layout.fillWidth: true
            implicitWidth: 0
            text: root.record && root.record.backgroundText ? root.record.backgroundText : qsTr("本次未设置背景资料。")
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            readOnly: true
            selectByMouse: true
            font.pixelSize: 15
            color: Theme.text
            padding: 0
            background: null
        }
    }
    Label {
        visible: !root.record
        text: qsTr("这条记录已不存在。")
        color: Theme.muted
        font.pixelSize: 14
    }

    Dialog {
        id: renameDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 48 : 440)
        modal: true
        title: qsTr("重命名对话")
        padding: 24
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.background
            radius: 9
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 20
            AppField {
                id: renameInput
                Layout.fillWidth: true
                placeholderText: qsTr("对话名称")
                selectByMouse: true
                maximumLength: 100
                Accessible.name: qsTr("新的对话名称")
                onAccepted: {
                    if (text.trim() && root.record) {
                        root.controller.renameRecord(root.record.id, text.trim());
                        renameDialog.close();
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item {
                    Layout.fillWidth: true
                }
                AppButton {
                    text: qsTr("取消")
                    variant: "secondary"
                    onClicked: renameDialog.close()
                }
                AppButton {
                    text: qsTr("保存")
                    enabled: renameInput.text.trim().length > 0
                    onClicked: {
                        if (root.record)
                            root.controller.renameRecord(root.record.id, renameInput.text.trim());
                        renameDialog.close();
                    }
                }
            }
        }
        onOpened: renameInput.forceActiveFocus()
    }
}
