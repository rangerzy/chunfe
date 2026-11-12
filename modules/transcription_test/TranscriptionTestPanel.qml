pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

Item {
    id: root

    property string status: "idle"
    property string elapsedText: "00:00"
    property string spkDevice: ""
    property string micDevice: ""
    property string spkState: "waiting"
    property string micState: "waiting"
    property real spkLevel: 0
    property real micLevel: 0
    property var lines: []
    property string errorMessage: ""
    readonly property bool active: status === "starting" || status === "running"
    readonly property string statusText: status === "running" ? qsTr("测试中") : status === "starting" ? qsTr("正在启动引擎") : status === "error" ? qsTr("测试异常") : status === "stopped" ? qsTr("已停止") : qsTr("等待测试")
    property bool initialized: false

    signal stopRequested
    signal restartRequested

    implicitWidth: 580
    implicitHeight: 360

    function followLatest() {
        transcript.following = true;
        transcript.positionViewAtEnd();
    }

    function updateScroll() {
        if (transcript.following) {
            Qt.callLater(function () {
                if (transcript.following)
                    transcript.positionViewAtEnd();
            });
        }
    }

    function syncLines() {
        if (!lines.length) {
            transcriptModel.clear();
            transcript.following = true;
            return;
        }
        // Keep existing delegates while partial text changes; array snapshots
        // must not reset the scroll position when the user reads earlier lines.
        const firstId = String(lines[0].id === undefined ? 0 : lines[0].id);
        while (transcriptModel.count && transcriptModel.get(0).entryId !== firstId)
            transcriptModel.remove(0);
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i];
            const value = {
                entryId: String(line.id === undefined ? i : line.id),
                source: line.source === "mic" ? "MIC" : "SPK",
                lineTime: String(line.time || ""),
                contentText: String(line.text || ""),
                complete: line.final !== false
            };
            if (i < transcriptModel.count && transcriptModel.get(i).entryId !== value.entryId)
                transcriptModel.remove(i, transcriptModel.count - i);
            if (i < transcriptModel.count)
                transcriptModel.set(i, value);
            else
                transcriptModel.append(value);
        }
        if (transcriptModel.count > lines.length)
            transcriptModel.remove(lines.length, transcriptModel.count - lines.length);
        updateScroll();
    }

    onLinesChanged: {
        if (initialized)
            syncLines();
    }
    Component.onCompleted: {
        initialized = true;
        syncLines();
    }

    ListModel {
        id: transcriptModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        AudioChannelStatus {
            objectName: "spkTestChannel"
            Layout.fillWidth: true
            channel: "SPK"
            channelName: qsTr("系统声音")
            deviceName: root.spkDevice
            inputState: root.spkState
            level: root.spkLevel
        }
        AudioChannelStatus {
            objectName: "micTestChannel"
            Layout.fillWidth: true
            channel: "MIC"
            channelName: qsTr("麦克风")
            deviceName: root.micDevice
            inputState: root.micState
            level: root.micLevel
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("转写文本")
                color: Theme.text
                font.pixelSize: 14
                font.weight: Font.Medium
            }
            Item {
                Layout.fillWidth: true
            }
            Text {
                text: root.elapsedText
                color: Theme.muted
                font.pixelSize: 12
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 76

            ListView {
                id: transcript
                objectName: "audioTestTranscript"
                anchors.fill: parent
                clip: true
                spacing: 8
                model: transcriptModel
                boundsBehavior: Flickable.StopAtBounds
                property bool following: true
                onMovementStarted: following = false
                onMovementEnded: following = atYEnd
                onContentHeightChanged: root.updateScroll()
                onHeightChanged: root.updateScroll()
                Accessible.role: Accessible.List
                Accessible.name: qsTr("测试转写文本")

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
                    required property string entryId
                    required property string source
                    required property string lineTime
                    required property string contentText
                    required property bool complete
                    width: Math.max(0, transcript.width - 10)
                    implicitHeight: Math.max(24, words.y + words.implicitHeight)
                    height: implicitHeight
                    Accessible.role: Accessible.ListItem
                    Accessible.name: source + "，" + lineTime + "，" + contentText

                    StatusBadge {
                        id: sourceLabel
                        text: line.source
                    }
                    Text {
                        id: timeLabel
                        x: sourceLabel.width + 10
                        y: 3
                        width: 42
                        text: line.lineTime
                        textFormat: Text.PlainText
                        color: Theme.muted
                        font.pixelSize: 12
                    }
                    Text {
                        id: words
                        x: timeLabel.x + timeLabel.width + 10
                        y: 2
                        width: Math.max(0, line.width - x)
                        text: line.contentText
                        textFormat: Text.PlainText
                        color: line.complete ? Theme.text : Theme.muted
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                        lineHeight: 1.35
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: transcriptModel.count === 0
                text: root.status === "starting" ? qsTr("引擎启动后开始转写") : root.status === "stopped" ? qsTr("本次测试暂无转写文本") : root.status === "error" ? qsTr("修复后可重新测试") : qsTr("暂无转写文本")
                color: Theme.muted
                font.pixelSize: 14
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            AppButton {
                objectName: "audioTestLatestButton"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                visible: !transcript.following && !transcript.atYEnd && transcriptModel.count > 0
                variant: "secondary"
                text: qsTr("回到最新")
                onClicked: root.followLatest()
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.errorMessage !== ""
            text: root.errorMessage
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: 12
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                objectName: "audioTestStatus"
                Layout.fillWidth: true
                text: root.statusText
                color: root.status === "error" ? Theme.danger : Theme.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            AppButton {
                objectName: "audioTestStopButton"
                text: qsTr("停止测试")
                variant: "secondary"
                enabled: root.active
                onClicked: root.stopRequested()
            }
            AppButton {
                objectName: "audioTestRestartButton"
                text: qsTr("重新测试")
                enabled: root.status !== "starting"
                onClicked: {
                    transcript.following = true;
                    root.restartRequested();
                }
            }
        }
    }
}
