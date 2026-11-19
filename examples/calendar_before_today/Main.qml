import QtQuick
import QtQuick.Controls.Basic

ApplicationWindow {
    id: window
    width: 492
    height: 532
    minimumWidth: 320
    minimumHeight: 360
    visible: true
    title: "日历"

    PastDateCalendar {
        objectName: "calendar"
        anchors.fill: parent
        // Change these values to adjust the earliest selectable month.
        minimumYear: 2026
        minimumMonth: 9
        dataDotColor: "#FA8C16"
        onMonthChanged: (year, month) => {
            // Replace with this month's dates when loading application data.
            dataDates = year === 2026 && month === 9 ? ["2026-09-23"] : ["2026-10-25"];
        }
        onDateSelected: date => console.log(Qt.formatDate(date, "yyyy-MM-dd"))
    }
}
