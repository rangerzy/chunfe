import QtQuick
import QtQuick.Controls

// Application policy attached by callers; TextBlock itself only displays text.
Control {
    id: actions

    property string text: ""
    property real focusRadius: 12
    property color focusColor: "#24352d"
    signal copyRequested(string text)

    padding: 0
    activeFocusOnTab: true
    background: null
    contentItem: null
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("复制文本")
    Accessible.description: qsTr("右键菜单或系统复制快捷键")
    Accessible.onPressAction: actions.copy()

    function copy(): void {
        copyRequested(text);
    }

    // The caller acknowledges a successful clipboard write before showing feedback.
    function showCopied(): void {
        copiedTip.open();
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: actions.focusRadius
        border.width: actions.visualFocus ? 1 : 0
        border.color: actions.focusColor
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: actions.forceActiveFocus(Qt.MouseFocusReason)
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: {
            actions.forceActiveFocus(Qt.MouseFocusReason);
            copyMenu.popup();
        }
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: actions.activeFocus
        onActivated: actions.copy()
    }
    Menu {
        id: copyMenu
        MenuItem {
            text: qsTr("复制全文")
            onTriggered: actions.copy()
        }
    }
    ToolTip {
        id: copiedTip
        objectName: "copyFeedback"
        text: qsTr("已复制")
        timeout: 1200
    }
}
