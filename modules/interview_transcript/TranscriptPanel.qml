pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui
import TextBlock

FloatingSurface {
    id: root

    property var messages: []
    property bool paused: false
    property bool finishing: false
    property bool collapsed: false
    property string elapsedText: "00:00"
    property string statusText: qsTr("春风正在聆听")
    property url logoSource
    property bool initialized: false
    readonly property int headerHeight: 64

    signal pauseRequested
    signal collapseRequested
    signal finishRequested
    signal dragRequested

    implicitWidth: 480
    implicitHeight: collapsed ? headerHeight : 368

    function followLatest() {
        transcript.following = true;
        transcript.positionViewAtEnd();
    }

    function updateScroll() {
        if (transcript.following) {
            Qt.callLater(function () {
                if (transcript.following)
                    root.followLatest();
            });
        }
    }

    function entryId(message, index) {
        return message.id === undefined ? "sample-" + index : String(message.id);
    }

    function syncMessages() {
        if (messages.length === 0) {
            transcriptModel.clear();
            transcript.following = true;
            return;
        }
        const firstId = entryId(messages[0], 0);
        while (transcriptModel.count && transcriptModel.get(0).entryId !== firstId)
            transcriptModel.remove(0);
        for (let i = 0; i < messages.length; ++i) {
            const message = messages[i];
            const value = {
                entryId: entryId(message, i),
                isMe: message.source === "mic" || (!message.source && message.role === "me"),
                channel: String(message.source || ""),
                contentText: String(message.text || ""),
                incomplete: message.incomplete === true
            };
            if (i < transcriptModel.count && transcriptModel.get(i).entryId !== value.entryId)
                transcriptModel.remove(i, transcriptModel.count - i);
            if (i < transcriptModel.count)
                transcriptModel.set(i, value);
            else
                transcriptModel.append(value);
        }
        if (transcriptModel.count > messages.length)
            transcriptModel.remove(messages.length, transcriptModel.count - messages.length);
        updateScroll();
    }

    onMessagesChanged: {
        if (initialized)
            syncMessages();
    }
    Component.onCompleted: {
        initialized = true;
        syncMessages();
    }
    onCollapsedChanged: {
        if (!collapsed)
            updateScroll();
    }

    ListModel { id: transcriptModel }

    Item {
        id: header
        width: parent.width
        height: root.headerHeight

        MouseArea {
            anchors.left: parent.left
            anchors.right: tools.left
            height: parent.height
            cursorShape: Qt.SizeAllCursor
            onPressed: root.dragRequested()
        }

        AppIcon {
            id: headerIcon
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            name: "audio"
            size: 24
            color: Theme.floatingText
        }

        Column {
            anchors.left: headerIcon.right
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(60, tools.x - x - 12)
            spacing: 4
            Text {
                width: parent.width
                text: root.paused && !root.finishing ? qsTr("春风已暂停") : root.statusText
                color: Theme.floatingText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.elapsedText + "  ·  " + (root.finishing ? qsTr("处理剩余音频")
                                                   : root.paused ? qsTr("暂停接收新内容") : qsTr("实时转写"))
                color: Theme.floatingMuted
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
                enabled: !root.finishing
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
                enabled: !root.finishing
                dark: true
                onClicked: root.finishRequested()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.floatingDivider
            visible: !root.collapsed
        }
    }

    Item {
        anchors.top: header.bottom
        anchors.topMargin: 12
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: !root.collapsed

        ListView {
            id: transcript
            objectName: "transcriptScroll"
            anchors.fill: parent
            anchors.bottomMargin: 0
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            model: transcriptModel
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

            delegate: Item {
                id: line
                required property bool isMe
                required property string channel
                required property string contentText
                required property bool incomplete
                width: transcript.width - 8
                height: Math.max(26, bubble.implicitHeight)

                Rectangle {
                    x: line.isMe ? line.width - width : 0
                    y: 3
                    width: line.channel ? 30 : 26
                    height: 26
                    radius: 13
                    color: line.isMe ? Theme.floatingMicSurface : Theme.floatingSpkSurface
                    border.color: Theme.floatingBorder
                    Text {
                        objectName: "liveTranscriptSource"
                        anchors.centerIn: parent
                        text: line.channel ? line.channel.toUpperCase() : line.isMe ? qsTr("我") : qsTr("对方")
                        color: Theme.floatingText
                        font.pixelSize: 12
                    }
                }

                TextBlock {
                    id: bubble
                    x: line.isMe ? line.width - width - 38 : 38
                    width: implicitWidth
                    height: implicitHeight
                    maximumWidth: Math.max(80, line.width - 76)
                    padding: 10
                    font.pixelSize: 15
                    textColor: Theme.floatingText
                    content: ({
                            text: line.contentText + (line.incomplete ? qsTr("（未完成）") : ""),
                            ranges: []
                        })
                    background: Rectangle {
                        color: line.isMe ? Theme.floatingMicSurface : Theme.floatingSpkSurface
                        radius: 10
                        border.color: Theme.floatingBorder
                    }
                }
            }
        }

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 35
            width: Math.min(parent.width - 64, 280)
            height: 82
            visible: transcript.count === 0
            Text {
                anchors.fill: parent
                anchors.margins: 16
                text: qsTr("等待对话…")
                color: Theme.floatingMuted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
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
                color: Theme.floatingText
                font: latest.font
                horizontalAlignment: Text.AlignHCenter
            }
            background: Rectangle {
                color: latest.hovered ? Theme.floatingControlHover : Theme.floatingControlSurface
                border.color: latest.visualFocus ? Theme.floatingText : Theme.floatingBorder
                radius: 6
            }
            onClicked: root.followLatest()
        }
    }
}
