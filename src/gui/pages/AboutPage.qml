import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    objectName: "aboutPage"
    maximumContentWidth: 640

    PageHeader {
        objectName: "aboutHeader"
        Layout.fillWidth: true
        title: qsTr("关于我们")
        subtitle: qsTr("版本 %1").arg(Qt.application.version)
    }

    Label {
        objectName: "aboutDescription"
        Layout.fillWidth: true
        Layout.maximumWidth: 520
        text: qsTr("春风是一款桌面端 AI 实时对话辅助工具，帮助你理解对方的问题、请求和异议，并参考回应建议组织表达。")
        color: Theme.text
        font.pixelSize: 14
        wrapMode: Text.Wrap
        lineHeight: 1.35
    }
}
