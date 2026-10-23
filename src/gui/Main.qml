import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    required property string fontLicenseText

    width: 960
    height: 640
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("春风")

    Label {
        anchors.centerIn: parent
        text: qsTr("春风")
        font.pixelSize: 28
    }

    footer: ToolBar {
        ToolButton {
            text: qsTr("本应用使用 OPPO Sans 字体 · 字体许可")
            onClicked: fontLicenseDialog.open()
        }
    }

    Dialog {
        id: fontLicenseDialog
        anchors.centerIn: parent
        width: Math.min(window.width - 48, 720)
        height: Math.min(window.height - 48, 520)
        modal: true
        title: qsTr("字体许可")
        standardButtons: Dialog.Close

        contentItem: ScrollView {
            id: licenseScroll
            clip: true
            contentWidth: availableWidth

            TextArea {
                width: licenseScroll.availableWidth
                text: window.fontLicenseText
                readOnly: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
            }
        }
    }
}
