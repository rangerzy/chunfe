pragma ComponentBehavior: Bound
import QtQuick
import Chunfe.Ui

Rectangle {
    id: root
    required property var controller
    readonly property bool anyHovered: home.hovered || history.hovered || background.hovered || model.hovered || audio.hovered || about.hovered
    implicitWidth: 64
    color: Theme.surface
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
    Image {
        x: 16; y: 18; width: 32; height: 32
        source: "qrc:/branding/chunfe-app.svg"
        fillMode: Image.PreserveAspectFit
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("春风")
    }
    Column {
        x: 10; y: 80; width: 44; spacing: 6
        NavItem { id: home; keyboardExpansionAllowed: !root.anyHovered; objectName: "navHome"; text: qsTr("开始对话"); iconName: "mic"; selected: root.controller.route === "home"; onClicked: root.controller.route = "home" }
        NavItem { id: history; keyboardExpansionAllowed: !root.anyHovered; objectName: "navHistory"; text: qsTr("对话记录"); iconName: "history"; selected: ["history","record"].includes(root.controller.route); onClicked: root.controller.route = "history" }
        Item { width: 44; height: 24; Rectangle { x: 10; y: 12; width: 24; height: 1; color: Theme.border } }
        NavItem { id: background; keyboardExpansionAllowed: !root.anyHovered; objectName: "navBackground"; text: qsTr("背景资料"); iconName: "file"; selected: root.controller.route === "background"; onClicked: root.controller.route = "background" }
        NavItem { id: model; keyboardExpansionAllowed: !root.anyHovered; objectName: "navModel"; text: qsTr("模型连接"); iconName: "settings"; selected: root.controller.route === "model"; onClicked: root.controller.route = "model" }
        NavItem { id: audio; keyboardExpansionAllowed: !root.anyHovered; objectName: "navAudio"; text: qsTr("音频与转写"); iconName: "audio"; selected: root.controller.route === "audio"; onClicked: root.controller.route = "audio" }
    }
    NavItem {
        id: about
        objectName: "navAbout"
        keyboardExpansionAllowed: !root.anyHovered
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        x: 10
        text: qsTr("关于我们")
        iconName: "info"
        selected: root.controller.route === "about"
        onClicked: root.controller.route = "about"
    }
}
