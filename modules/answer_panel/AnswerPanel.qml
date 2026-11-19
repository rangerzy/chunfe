pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui
import TextBlock

Rectangle {
    id: root

    property string heading: qsTr("回应建议")
    property string question: ""
    property string triggerKind: ""
    property string answer: ""
    property bool generating: false
    property string pendingQuestion: ""
    property string statusMessage: ""
    property bool collapsed: false
    readonly property bool waitingForContent: !question && !answer && !generating
    readonly property int headerHeight: 54

    signal collapseRequested
    signal dragRequested

    implicitWidth: 400
    implicitHeight: collapsed ? headerHeight : 460
    radius: 8
    color: "#18181b"
    border.color: "#3f3f46"

    onQuestionChanged: body.contentY = 0

    Item {
        id: header
        width: parent.width
        height: root.headerHeight

        MouseArea {
            anchors.left: parent.left
            anchors.right: fold.left
            height: parent.height
            cursorShape: Qt.SizeAllCursor
            onPressed: root.dragRequested()
        }

        AppIcon {
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            name: "bolt"
            size: 18
            color: "#fafafa"
        }
        Text {
            x: 45
            anchors.verticalCenter: parent.verticalCenter
            text: root.heading
            color: "#fafafa"
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        IconButton {
            id: fold
            objectName: "answerCollapseButton"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            iconName: root.collapsed ? "expand" : "collapse"
            text: root.collapsed ? qsTr("展开建议") : qsTr("收起建议")
            dark: true
            onClicked: root.collapseRequested()
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: "#3f3f46"
            visible: !root.collapsed
        }
    }

    Column {
        id: notices
        anchors.top: header.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 8
        visible: !root.collapsed

        Rectangle {
            width: parent.width
            // Keep the preview bounded independently of wrapped/elided text
            // metrics when the hosting window collapses or changes screens.
            height: root.pendingQuestion.length > 0 ? 62 : 0
            visible: root.pendingQuestion.length > 0
            radius: 6
            color: "#27272a"
            Text {
                id: pendingLabel
                x: 10
                y: 9
                width: parent.width - 20
                height: Math.max(0, parent.height - 18)
                text: qsTr("正在准备新的回应建议…\n") + root.pendingQuestion
                textFormat: Text.PlainText
                color: "#d4d4d8"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
        }

        Text {
            width: parent.width
            visible: root.generating
            text: qsTr("正在生成回应建议…")
            font.pixelSize: 12
            color: "#d4d4d8"
        }
        Text {
            width: parent.width
            visible: !root.waitingForContent && text.length > 0
            text: root.statusMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.pixelSize: 12
            color: "#d4d4d8"
        }
    }

    Flickable {
        id: body
        objectName: "answerScroll"
        anchors.top: notices.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 18
        anchors.rightMargin: 12
        anchors.bottomMargin: 14
        contentWidth: width
        contentHeight: answerContent.height
        clip: true
        visible: !root.collapsed
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: answerContent
            width: body.width - 8
            spacing: 14

            Text {
                visible: root.question.length > 0
                text: qsTr("对应对话") + (root.triggerKind === "request" ? qsTr(" · 请求") : root.triggerKind === "objection" ? qsTr(" · 异议") : root.triggerKind === "question" ? qsTr(" · 问题") : "")
                color: "#d4d4d8"
                font.pixelSize: 12
            }
            Text {
                objectName: "adviceQuestionText"
                width: parent.width
                visible: root.question.length > 0
                text: root.question
                textFormat: Text.PlainText
                color: "#d4d4d8"
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
            Rectangle {
                width: parent.width
                height: 1
                color: "#3f3f46"
                visible: root.answer.length > 0
            }
            Repeater {
                model: root.answer.length ? root.answer.split("\n\n") : []
                delegate: TextBlock {
                    id: paragraph
                    required property string modelData
                    required property int index
                    width: implicitWidth
                    height: implicitHeight
                    maximumWidth: answerContent.width
                    font.pixelSize: index === 0 ? 16 : 15
                    font.weight: index === 0 ? Font.DemiBold : Font.Normal
                    textColor: "#fafafa"
                    content: ({
                            text: paragraph.modelData,
                            ranges: []
                        })
                }
            }
            Column {
                width: parent.width
                visible: root.waitingForContent
                spacing: 12
                topPadding: 42
                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "bolt"
                    size: 28
                    color: "#d4d4d8"
                }
                Text {
                    objectName: "answerEmptyState"
                    width: parent.width
                    text: root.statusMessage || qsTr("等待需要回应的内容")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    color: "#fafafa"
                    font.pixelSize: 16
                }
            }
        }
    }
}
