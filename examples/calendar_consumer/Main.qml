pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Calendar

ApplicationWindow {
    id: window
    property bool projectFonts: false
    property string fontLicenseText: ""
    readonly property date minimumDate: new Date(calendar.today.getFullYear(), calendar.today.getMonth() - 3, 1)

    width: 492
    height: 612
    minimumWidth: 320
    minimumHeight: 440
    visible: true
    title: qsTr("Calendar · 独立接入示例")
    color: "#f5f6f8"

    function datesWithData(year: int, month: int): list<string> {
        const dates = [];
        for (const day of [1, 6, 14, 22]) {
            const date = new Date(year, month - 1, day);
            if (date <= calendar.today)
                dates.push(Qt.formatDate(date, "yyyy-MM-dd"));
        }
        return dates;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PastDateCalendar {
            id: calendar
            objectName: "calendar"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 532
            minimumYear: window.minimumDate.getFullYear()
            minimumMonth: window.minimumDate.getMonth() + 1
            dataDotColor: "#FA8C16"
            onMonthChanged: (year, month) => {
                // Replace this sample data with records for the requested month.
                dataDates = window.datesWithData(year, month);
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: "#e4e7ed"
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: qsTr("已选日期：%1").arg(Qt.formatDate(calendar.selectedDate, "yyyy-MM-dd"))
                color: "#252930"
                font.pixelSize: 15
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Rectangle {
                    width: 5
                    height: 5
                    radius: width / 2
                    color: calendar.dataDotColor
                }
                Text {
                    text: qsTr("当天有记录")
                    color: "#9399a5"
                    font.pixelSize: 12
                }
                Item {
                    Layout.fillWidth: true
                }
                ToolButton {
                    visible: window.projectFonts
                    text: qsTr("字体许可")
                    font.pixelSize: 12
                    padding: 0
                    onClicked: fontLicenseDialog.open()
                }
            }
        }
    }

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
}
