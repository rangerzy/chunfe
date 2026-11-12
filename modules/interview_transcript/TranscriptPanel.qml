pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui
import TextBlock

Item {
    id: root

    property var messages: []
    property bool paused: false
    property bool collapsed: false
    property string elapsedText: "00:00"
    property string statusText: qsTr("春风正在聆听")
    property url logoSource
    readonly property int headerHeight: 64

    signal pauseRequested
    signal collapseRequested
    signal finishRequested
    signal dragRequested

    implicitWidth: 480
    implicitHeight: collapsed ? headerHeight : 368

    function followLatest() {
        transcript.following = true;
        transcript.contentY = Math.max(0, transcript.contentHeight - transcript.height);
    }

    function updateScroll() {
        if (transcript.following) {
            Qt.callLater(function () {
                if (transcript.following)
                    root.followLatest();
            });
        }
    }

    onMessagesChanged: updateScroll()
    onCollapsedChanged: {
        if (!collapsed)
            updateScroll();
    }

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        radius: 8
        color: "#18181b"
        border.color: "#3f3f46"

        MouseArea {
            anchors.left: parent.left
            anchors.right: tools.left
            height: parent.height
            cursorShape: Qt.SizeAllCursor
            onPressed: root.dragRequested()
        }

        Rectangle {
            id: logo
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 46
            height: 46
            radius: 23
            color: "#fafafa"
            Image {
                anchors.centerIn: parent
                width: 36
                height: 36
                source: root.logoSource
                fillMode: Image.PreserveAspectFit
            }
        }

        Column {
            anchors.left: logo.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(60, tools.x - x - 12)
            spacing: 4
            Text {
                width: parent.width
                text: root.paused ? qsTr("春风已暂停") : root.statusText
                color: "#fafafa"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.elapsedText + "  ·  " + (root.paused ? qsTr("暂停接收新内容") : qsTr("实时转写"))
                color: "#d4d4d8"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Row {
            id: tools
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconButton {
                objectName: "transcriptPauseButton"
                iconName: root.paused ? "play" : "pause"
                text: root.paused ? qsTr("继续对话") : qsTr("暂停对话")
                dark: true
                onClicked: root.pauseRequested()
            }
            IconButton {
                objectName: "transcriptCollapseButton"
                iconName: root.collapsed ? "expand" : "collapse"
                text: root.collapsed ? qsTr("展开对话") : qsTr("收起对话")
                dark: true
                onClicked: root.collapseRequested()
            }
            IconButton {
                objectName: "interviewFinishButton"
                iconName: "stop"
                text: qsTr("结束对话")
                dark: true
                onClicked: root.finishRequested()
            }
        }
    }

    Item {
        anchors.top: header.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.collapsed
        opacity: 1

        Flickable {
            id: transcript
            objectName: "transcriptScroll"
            anchors.fill: parent
            anchors.bottomMargin: 0
            anchors.leftMargin: 2
            anchors.rightMargin: 2
            contentWidth: width
            contentHeight: lines.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            property bool following: true
            onMovementStarted: following = false
            onMovementEnded: following = atYEnd
            onContentHeightChanged: root.updateScroll()
            onHeightChanged: root.updateScroll()

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                onPressedChanged: {
                    if (pressed)
                        transcript.following = false;
                    else
                        transcript.following = transcript.atYEnd;
                }
            }

            Column {
                id: lines
                width: transcript.width - 8
                spacing: 8
                Repeater {
                    model: root.messages
                    delegate: Item {
                        id: line
                        required property var modelData
                        readonly property bool isMe: modelData.role === "me"
                        width: lines.width
                        height: Math.max(26, bubble.implicitHeight)

                        Rectangle {
                            x: line.isMe ? line.width - width : 0
                            y: 3
                            width: 26
                            height: 26
                            radius: 13
                            color: line.isMe ? "#3f3f46" : "#18181b"
                            border.color: "#52525b"
                            Text {
                                anchors.centerIn: parent
                                text: line.isMe ? qsTr("我") : qsTr("对方")
                                color: "#e4e4e7"
                                font.pixelSize: 12
                            }
                        }

                        TextBlock {
                            id: bubble
                            x: line.isMe ? line.width - width - 34 : 34
                            width: implicitWidth
                            height: implicitHeight
                            maximumWidth: Math.max(80, line.width - 76)
                            padding: 10
                            font.pixelSize: 15
                            textColor: "#fafafa"
                            content: ({
                                    text: String(line.modelData.text || ""),
                                    ranges: []
                                })
                            background: Rectangle {
                                color: line.isMe ? "#3f3f46" : "#18181b"
                                radius: 10
                                border.color: "#52525b"
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: transcript.contentY + 35
                width: Math.min(parent.width - 64, 280)
                height: 82
                visible: root.messages.length === 0
                color: "#18181b"
                radius: 8
                Text {
                    anchors.fill: parent
                    anchors.margins: 16
                    text: qsTr("等待对话…")
                    color: "#d4d4d8"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.Wrap
                }
            }
        }

        Button {
            id: latest
            objectName: "transcriptLatestButton"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            visible: !transcript.following && !transcript.atYEnd
            text: qsTr("回到最新")
            padding: 9
            font.pixelSize: 12
            contentItem: Text {
                text: latest.text
                color: "#fafafa"
                font: latest.font
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                color: latest.hovered ? "#3f3f46" : "#18181b"
                border.color: latest.visualFocus ? "#fafafa" : "#52525b"
                radius: 6
            }
            onClicked: root.followLatest()
        }
    }
}
