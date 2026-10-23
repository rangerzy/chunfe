pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import TextBlock

Window {
    id: window
    property bool projectFonts: false
    property string fontLicenseText: ""

    TextClipboard { id: clipboard }
    component CopyActions: TextCopyActions {
        id: copyActions
        anchors.fill: parent
        onCopyRequested: function(text) {
            if (clipboard.copyText(text))
                copyActions.showCopied();
        }
    }

    width: 640
    height: 480
    minimumWidth: 320
    minimumHeight: 400
    visible: true
    title: qsTr("TextBlock · 独立接入示例")
    color: "#f4f5f1"

    Dialog {
        id: fontLicenseDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: window.width - 40
        height: window.height - 40
        modal: true
        title: qsTr("字体许可")
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent
            TextArea {
                text: window.fontLicenseText
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.pixelSize: 12
            }
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 28
        contentWidth: width
        contentHeight: samples.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: samples
            width: parent.width
            spacing: 18

            Text {
                text: qsTr("TextBlock / 独立模块")
                font.pixelSize: 24
                font.weight: Font.DemiBold
                color: "#24352d"
            }
            Text {
                visible: window.projectFonts
                width: parent.width
                text: qsTr("中文：OPPO Sans 4.0  ·  English / 0123：JetBrains Mono")
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6d7b73"
            }
            Text {
                width: parent.width
                text: qsTr("拖动窗口观察换行；右键气泡或聚焦后按 Ctrl/Cmd+C 复制。")
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: "#6d7b73"
            }
            TextBlock {
                id: styledBlock
                maximumWidth: parent.width
                padding: 16
                font.pixelSize: 18
                textColor: "#24352d"
                background: Rectangle { color: "#edf3e8"; radius: 12 }
                highlightColor: "#e2eeab"
                CopyActions { text: String(styledBlock.content.text ?? "") }
                content: ({
                        text: "先理解问题，再给出清晰的结论。",
                        ranges: [
                            {
                                start: 9,
                                end: 14,
                                bold: true
                            },
                            {
                                start: 7,
                                end: 11,
                                highlight: true
                            }
                        ]
                    })
            }
            TextBlock {
                id: mixedBlock
                maximumWidth: parent.width
                padding: 16
                font.pixelSize: 18
                textColor: "#24352d"
                background: Rectangle { color: "#ffffff"; radius: 12 }
                highlightColor: "#e2eeab"
                CopyActions { text: String(mixedBlock.content.text ?? "") }
                content: ({
                        text: "Hello, Qt Quick! 你好，世界 🌱 每个气泡只包含一个段落。这个示例仅通过模块目录接入，不依赖 Chunfe 的应用代码或主题。",
                        ranges: [
                            {
                                start: 7,
                                end: 15,
                                bold: true
                            }
                        ]
                    })
            }
            Text {
                width: parent.width
                text: qsTr("静态 QML 模块 · URI TextBlock · C++23 / Qt 6.12")
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: "#6d7b73"
            }
            Button {
                visible: window.projectFonts
                text: qsTr("字体许可")
                onClicked: fontLicenseDialog.open()
            }
        }
    }
}
