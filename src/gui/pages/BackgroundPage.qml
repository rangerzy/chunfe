pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    maximumContentWidth: 640

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("背景资料")
        subtitle: qsTr("保存一次，之后的对话会自动使用。")
    }

    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("相关资料与沟通偏好")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Item {
                Layout.fillWidth: true
            }
            StatusBadge {
                text: root.controller.backgroundText ? qsTr("已保存") : qsTr("尚未设置")
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
            color: Theme.background
            border.color: editor.activeFocus ? Theme.text : Theme.border
            radius: Theme.radius

            Flickable {
                id: editorViewport
                anchors.fill: parent
                anchors.margins: 14
                clip: true
                contentWidth: width
                contentHeight: Math.max(height, editor.contentHeight)
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                ScrollBar.vertical: ScrollBar {}

                function revealCursor() {
                    if (!editor.activeFocus)
                        return;
                    if (editor.cursorRectangle.y < contentY)
                        contentY = editor.cursorRectangle.y;
                    else if (editor.cursorRectangle.y + editor.cursorRectangle.height > contentY + height)
                        contentY = editor.cursorRectangle.y + editor.cursorRectangle.height - height;
                }

                TextEdit {
                    id: editor
                    objectName: "backgroundEditor"
                    width: editorViewport.width
                    height: Math.max(editorViewport.height, contentHeight)
                    text: root.controller.backgroundDraft
                    onTextChanged: {
                        if (root.controller.backgroundDraft !== text)
                            root.controller.backgroundDraft = text;
                    }
                    onCursorRectangleChanged: editorViewport.revealCursor()
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    font.pixelSize: 14
                    color: Theme.text
                    selectionColor: "#e4e4e7"
                    selectedTextColor: Theme.text
                    Accessible.name: qsTr("背景资料")
                    Accessible.role: Accessible.EditableText

                    Label {
                        width: parent.width
                        visible: editor.text.length === 0
                        text: qsTr("填写身份、业务资料与沟通偏好，或导入 Markdown。")
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font.pixelSize: 14
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: root.controller.backgroundDraft !== root.controller.backgroundText ? qsTr("有未保存的修改") : root.controller.backgroundName || qsTr("背景资料可选")
                elide: Text.ElideMiddle
                color: Theme.muted
                font.pixelSize: 12
            }
            AppButton {
                text: qsTr("导入 Markdown")
                variant: "secondary"
                onClicked: root.controller.importMockBackground()
            }
            AppButton {
                objectName: "saveBackgroundButton"
                text: qsTr("保存背景")
                onClicked: root.controller.saveBackground(editor.text)
            }
        }
    }
}
