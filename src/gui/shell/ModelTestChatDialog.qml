pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

Dialog {
    id: root
    required property var controller
    readonly property bool running: controller?.running ?? false
    readonly property bool canSend: !!controller && !running && !prompt.inputMethodComposing
                                    && prompt.text.trim().length > 0
    objectName: "modelTestChatDialog"
    title: qsTr("测试对话")
    anchors.centerIn: parent
    width: Math.min(620, parent ? parent.width - 40 : 620)
    height: Math.min(470, parent ? parent.height - 40 : 470)
    padding: 20
    topPadding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    onOpened: prompt.forceActiveFocus()
    onAboutToHide: {
        root.controller?.endSession();
        prompt.clear();
        scrollUpdate.stop();
        scrollUpdate.force = false;
    }

    function sendPrompt() {
        if (!canSend || !controller.sendPrompt(prompt.text))
            return;
        timeline.selectedReply?.deselect();
        prompt.clear();
        prompt.forceActiveFocus();
        scrollUpdate.force = true;
        scrollUpdate.restart();
    }

    background: Rectangle {
        radius: 16
        color: Theme.background
        border.color: Theme.border
    }
    header: Item {
        implicitHeight: 56
        Label {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.text
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        IconButton {
            objectName: "modelTestChatCloseButton"
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            iconName: "close"
            text: qsTr("关闭测试对话")
            onClicked: root.close()
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            objectName: "modelTestChatModel"
            Layout.fillWidth: true
            text: qsTr("模型：%1").arg(root.controller?.modelName ?? "")
            color: Theme.muted
            textFormat: Text.PlainText
        }
        ScrollView {
            id: timeline
            objectName: "modelTestChatTimeline"
            readonly property Flickable viewport: contentItem as Flickable
            property real previousContentHeight: 0
            property real previousViewportHeight: 0
            property TextArea selectedReply: null
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 100
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            function scheduleFollow() {
                const previousEnd = viewport.originY + Math.max(0, previousContentHeight - previousViewportHeight);
                previousContentHeight = viewport.contentHeight;
                previousViewportHeight = viewport.height;
                if (scrollUpdate.running || viewport.contentY < previousEnd - 1)
                    return;
                scrollUpdate.previousPosition = viewport.contentY;
                scrollUpdate.start();
            }

            Timer {
                id: scrollUpdate
                property bool force: false
                property real previousPosition: 0
                interval: 0
                onTriggered: {
                    const view = timeline.viewport;
                    const follow = force || (view && !view.moving && !timeline.selectedReply
                                             && Math.abs(view.contentY - previousPosition) <= 1);
                    force = false;
                    if (root.opened && view && follow)
                        view.contentY = view.originY + Math.max(0, view.contentHeight - view.height);
                }
            }
            Connections {
                target: timeline.viewport
                function onContentHeightChanged() { timeline.scheduleFollow(); }
                function onHeightChanged() { timeline.scheduleFollow(); }
            }

            Column {
                width: timeline.availableWidth
                spacing: 18

                Repeater {
                    id: turns
                    objectName: "modelTestChatTurns"
                    model: root.controller?.turns ?? null
                    delegate: Column {
                        id: turn
                        required property int index
                        required property string promptText
                        required property string replyText
                        required property string turnState
                        required property string turnMessage
                        readonly property bool retryAvailable: turnState === "failed" && index === turns.count - 1
                        width: timeline.availableWidth
                        spacing: 8

                        Rectangle {
                            anchors.right: parent.right
                            width: Math.min(turn.width * 0.82, userText.implicitWidth + 24)
                            height: userText.implicitHeight + 18
                            color: Theme.hover
                            radius: Theme.radius

                            Label {
                                id: userText
                                x: 12
                                y: 9
                                width: parent.width - 24
                                text: turn.promptText
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: Theme.text
                                font.pixelSize: 14
                            }
                        }
                        TextArea {
                            id: reply
                            objectName: "modelTestChatResponse"
                            readonly property string incomingText: turn.replyText
                            width: turn.width
                            visible: length > 0
                            Accessible.name: qsTr("模型回复")
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            readOnly: true
                            selectByMouse: true
                            color: Theme.text
                            font.pixelSize: 14
                            padding: 4
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
                            onSelectedTextChanged: {
                                if (reply.selectedText.length > 0)
                                    timeline.selectedReply = reply;
                                else if (timeline.selectedReply === reply)
                                    timeline.selectedReply = null;
                            }
                        }
                        RowLayout {
                            width: turn.width
                            visible: status.text.length > 0 || turn.retryAvailable
                            spacing: 10
                            Label {
                                id: status
                                objectName: "modelTestChatStatus"
                                Layout.fillWidth: true
                                text: turn.turnMessage || (turn.turnState === "streaming" ? qsTr("正在生成…") : "")
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: turn.turnState === "failed" ? Theme.danger : Theme.muted
                                onTextChanged: {
                                    if (root.opened && status.text.length > 0)
                                        Accessible.announce(status.text);
                                }
                            }
                            AppButton {
                                objectName: "modelTestChatRetryButton"
                                variant: "secondary"
                                text: qsTr("重试")
                                visible: turn.retryAvailable
                                enabled: !root.running
                                onClicked: {
                                    if (root.controller.retryLast())
                                        prompt.forceActiveFocus();
                                }
                            }
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(56, Math.min(128, prompt.contentHeight + prompt.topPadding + prompt.bottomPadding + 2))
            color: Theme.background
            border.color: prompt.activeFocus ? Theme.ring : Theme.border
            radius: Theme.radius

            ScrollView {
                anchors.fill: parent
                anchors.margins: 1
                anchors.rightMargin: 48
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                TextArea {
                    id: prompt
                    objectName: "modelTestChatPrompt"
                    Accessible.name: qsTr("输入消息")
                    placeholderText: qsTr("输入要发送给 DeepSeek 的消息")
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    font.pixelSize: 14
                    padding: 12
                    background: null

                    Keys.onPressed: event => {
                        if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                            return;
                        if (prompt.inputMethodComposing || (event.modifiers & ~Qt.KeypadModifier) !== Qt.NoModifier)
                            return;
                        event.accepted = true;
                        if (!event.isAutoRepeat)
                            root.sendPrompt();
                    }
                }
            }
            IconButton {
                id: send
                objectName: "modelTestChatSendButton"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 9
                text: qsTr("发送")
                enabled: root.canSend
                focusPolicy: Qt.TabFocus
                contentItem: AppIcon {
                    name: "arrow-right"
                    size: 18
                    color: Theme.primaryText
                    rotation: -90
                }
                background: Rectangle {
                    radius: width / 2
                    color: Theme.primary
                    border.width: send.visualFocus ? 2 : 0
                    border.color: Theme.ring
                }
                onClicked: root.sendPrompt()
            }
        }
    }
}
