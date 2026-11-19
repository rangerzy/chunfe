pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    property string searchText: ""
    readonly property bool storageUnavailable: controller.conversationStore?.ready === false
    readonly property var filteredRecords: controller.records.filter(function (record) {
        return String(record.title).toLowerCase().indexOf(root.searchText.trim().toLowerCase()) !== -1;
    })

    RowLayout {
        Layout.fillWidth: true
        spacing: 16
        PageHeader {
            Layout.fillWidth: true
            title: qsTr("对话记录")
            subtitle: root.controller.isDemo ? qsTr("回顾对话，以及当时的回应建议。") : qsTr("回顾已保存的转写与背景资料。")
        }
        AppButton {
            text: qsTr("新建对话")
            onClicked: root.controller.startChecks()
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.storageUnavailable
        text: root.controller.conversationStore?.errorMessage || qsTr("无法打开对话记录数据库。")
        textFormat: Text.PlainText
        color: Theme.danger
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        AppIcon {
            name: "search"
            size: 18
            color: Theme.muted
        }
        AppField {
            objectName: "historySearchField"
            Layout.fillWidth: true
            Layout.maximumWidth: 330
            placeholderText: qsTr("搜索对话名称")
            text: root.searchText
            onTextEdited: root.searchText = text
            Accessible.name: qsTr("搜索对话名称")
        }
        Item {
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("%1 次对话").arg(root.controller.records.length)
            color: Theme.muted
            font.pixelSize: 12
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: root.filteredRecords
            delegate: Rectangle {
                id: recordRow
                required property var modelData
                readonly property int questionCount: modelData.questionCount ?? modelData.questions?.length ?? 0
                Layout.fillWidth: true
                implicitHeight: recordRowContent.implicitHeight + 40
                radius: 8
                border.color: Theme.border
                color: Theme.background
                RowLayout {
                    id: recordRowContent
                    x: 20
                    y: 20
                    width: parent.width - 40
                    spacing: 16
                    Rectangle {
                        implicitWidth: 38
                        implicitHeight: 38
                        radius: 8
                        color: Theme.surface
                        border.color: Theme.border
                        AppIcon {
                            anchors.centerIn: parent
                            name: "history"
                            size: 18
                            color: Theme.muted
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Label {
                            Layout.fillWidth: true
                            text: recordRow.modelData.title
                            textFormat: Text.PlainText
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            color: Theme.text
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(recordRow.modelData.date || "") + " · " + String(recordRow.modelData.duration || "")
                                  + (recordRow.questionCount > 0 ? " · " + qsTr("%1 条建议").arg(recordRow.questionCount) : "")
                            textFormat: Text.PlainText
                            color: Theme.muted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    AppButton {
                        text: qsTr("查看记录")
                        variant: "ghost"
                        onClicked: root.controller.openRecord(recordRow.modelData.id)
                    }
                    IconButton {
                        visible: root.bodyWidth > 650 && recordRow.modelData.isDemo
                        iconName: "download"
                        text: qsTr("导出 Markdown · %1").arg(recordRow.modelData.title)
                        onClicked: root.controller.exportRecord(recordRow.modelData.id)
                    }
                    IconButton {
                        iconName: "trash"
                        text: qsTr("删除 %1").arg(recordRow.modelData.title)
                        onClicked: root.controller.deleteRecord(recordRow.modelData.id)
                    }
                }
            }
        }
        FormCard {
            Layout.fillWidth: true
            visible: root.filteredRecords.length === 0 && !root.storageUnavailable
            padding: 40
            AppIcon {
                Layout.alignment: Qt.AlignHCenter
                name: "history"
                size: 30
                color: Theme.muted
            }
            Label {
                Layout.fillWidth: true
                text: root.searchText ? qsTr("没有找到匹配的对话") : qsTr("暂无对话记录")
                horizontalAlignment: Text.AlignHCenter
                color: Theme.text
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Label {
                Layout.fillWidth: true
                text: root.searchText ? qsTr("试试其他关键词。") : qsTr("结束对话后，文字记录会出现在这里。")
                horizontalAlignment: Text.AlignHCenter
                color: Theme.muted
                font.pixelSize: 12
            }
            AppButton {
                Layout.alignment: Qt.AlignHCenter
                visible: !root.searchText
                variant: "secondary"
                text: qsTr("开始对话")
                onClicked: root.controller.startChecks()
            }
        }
    }
}
