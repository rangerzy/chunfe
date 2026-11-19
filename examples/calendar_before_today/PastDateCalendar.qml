pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Pane {
    id: calendar

    // Public range configuration. Months use 1–12; the first day is inclusive.
    property int minimumYear: 2026
    property int minimumMonth: 10
    property date today: new Date()
    property date selectedDate: today
    property int displayedYear: today.getFullYear()
    property int displayedMonth: today.getMonth() + 1
    property int animationDuration: 240
    property real liquidStretch: 1.0
    // Local calendar dates in yyyy-MM-dd format. Consumers replace this list
    // from onMonthChanged when supplying markers for the displayed month.
    property list<string> dataDates: []
    property color dataDotColor: "#FA8C16"

    property bool _ready: false
    property bool _selecting: false
    property int _pendingMonth: -1
    property int _notifiedMonthIndex: -1

    signal dateSelected(date date)
    // Emitted once on initialization and after each actual view-month change.
    // The year/month state is complete; month uses 1–12.
    signal monthChanged(int year, int month)

    readonly property int firstMonthIndex: minimumYear * 12 + minimumMonth - 1
    readonly property int lastMonthIndex: today.getFullYear() * 12 + today.getMonth()
    readonly property int displayedMonthIndex: displayedYear * 12 + displayedMonth - 1
    readonly property int firstDayKey: minimumYear * 10000 + minimumMonth * 100 + 1
    readonly property real uiScale: Math.min(1, width / implicitWidth, height / implicitHeight)
    readonly property color textColor: "#252930"
    readonly property color mutedColor: "#9399a5"
    readonly property color accentColor: "#1b55f5"

    implicitWidth: 492
    implicitHeight: 532
    padding: 22 * uiScale
    topPadding: 24 * uiScale
    bottomPadding: 20 * uiScale
    font.pixelSize: 28 * uiScale

    Component.onCompleted: {
        _ready = true;
        Qt.callLater(syncIndicator);
        Qt.callLater(notifyMonthChanged);
    }
    onDisplayedYearChanged: resetIndicator()
    onDisplayedMonthChanged: resetIndicator()
    onDisplayedMonthIndexChanged: {
        // Coalesce external year/month assignments so crossing a year boundary
        // never notifies consumers with a partially updated pair.
        if (_ready)
            Qt.callLater(notifyMonthChanged);
    }
    onSelectedDateChanged: {
        if (!_selecting)
            resetIndicator();
    }
    onMinimumYearChanged: resetIndicator()
    onMinimumMonthChanged: resetIndicator()
    onTodayChanged: resetIndicator()

    function dayKey(date: date): int {
        return date.getFullYear() * 10000 + (date.getMonth() + 1) * 100 + date.getDate();
    }

    function isSelectable(date: date): bool {
        const key = dayKey(date);
        return key >= firstDayKey && key <= dayKey(today);
    }

    function showMonth(index: int): void {
        if (index < firstMonthIndex || index > lastMonthIndex)
            return;
        resetIndicator();
        displayedYear = Math.floor(index / 12);
        displayedMonth = index % 12 + 1;
        notifyMonthChanged();
    }

    function notifyMonthChanged(): void {
        if (!_ready || _notifiedMonthIndex === displayedMonthIndex)
            return;
        // Record before emitting: a consumer may synchronously navigate again.
        _notifiedMonthIndex = displayedMonthIndex;
        monthChanged(displayedYear, displayedMonth);
    }

    function positionForDate(date: date): var {
        const key = dayKey(date);
        for (const item of grid.contentItem.children) {
            if (item.dateKey === key)
                return item.mapToItem(liquid, item.width / 2, item.height / 2);
        }
        return null;
    }

    function syncIndicator(): void {
        // A newer click may already have started a flight before this deferred
        // layout synchronization runs. Do not snap over that newer interaction.
        if (!_ready || liquid.running)
            return;
        const position = positionForDate(selectedDate);
        if (position !== null && isSelectable(selectedDate))
            liquid.snapTo(position);
        else
            liquid.clear();
    }

    function resetIndicator(): void {
        if (!_ready)
            return;
        _pendingMonth = -1;
        liquid.clear();
        // MonthGrid updates its delegates and geometry after the month changes.
        Qt.callLater(syncIndicator);
    }

    function selectDate(date: date): void {
        if (!isSelectable(date))
            return;
        const repeated = dayKey(date) === dayKey(selectedDate);
        const monthIndex = date.getFullYear() * 12 + date.getMonth();
        _selecting = true;
        selectedDate = new Date(date.getFullYear(), date.getMonth(), date.getDate());
        _selecting = false;

        if (repeated) {
            if (!liquid.running && monthIndex !== displayedMonthIndex)
                showMonth(monthIndex);
        } else {
            _pendingMonth = monthIndex !== displayedMonthIndex ? monthIndex : -1;
            const position = positionForDate(selectedDate);
            if (position === null) {
                // Programmatic selection outside the visible grid has no flight path.
                showMonth(monthIndex);
            } else if (liquid.visible) {
                liquid.flowTo(position);
            } else {
                liquid.gatherAt(position);
            }
        }
        // Consumers may synchronously select another date or navigate. Their
        // newer action must not be overwritten by this click's continuation.
        dateSelected(selectedDate);
    }

    function yearOptions(): list<var> {
        const result = [];
        for (let year = minimumYear; year <= today.getFullYear(); ++year)
            result.push({
                value: year,
                label: year + "年"
            });
        return result;
    }

    function monthOptions(year: int): list<var> {
        const first = year === minimumYear ? minimumMonth : 1;
        const last = year === today.getFullYear() ? today.getMonth() + 1 : 12;
        const result = [];
        for (let month = first; month <= last; ++month)
            result.push({
                value: month,
                label: month + "月"
            });
        return result;
    }

    // Keep the upper bound current when the example stays open overnight.
    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: {
            const now = new Date();
            if (calendar.dayKey(now) !== calendar.dayKey(calendar.today))
                calendar.today = now;
        }
    }

    background: Rectangle {
        color: "#f5f6f8"
    }

    component ArrowButton: ToolButton {
        id: arrow
        property bool forward: false
        implicitWidth: 48 * calendar.uiScale
        implicitHeight: 48 * calendar.uiScale
        hoverEnabled: true
        Accessible.name: forward ? "下个月" : "上个月"

        contentItem: Item {
            // Two strokes form a chevron without relying on an icon font.
            Item {
                anchors.centerIn: parent
                width: 12 * calendar.uiScale
                height: 20 * calendar.uiScale
                rotation: arrow.forward ? 180 : 0

                Repeater {
                    model: 2
                    Rectangle {
                        required property int index
                        x: 3 * calendar.uiScale
                        y: (index === 0 ? 1 : 9) * calendar.uiScale
                        width: 3 * calendar.uiScale
                        height: 12 * calendar.uiScale
                        radius: width / 2
                        rotation: index === 0 ? 40 : -40
                        color: arrow.enabled ? "#666e79" : "#c8ccd3"
                    }
                }
            }
        }
        background: Rectangle {
            radius: 8
            color: arrow.hovered ? "#e8ebf0" : "transparent"
            border.width: arrow.visualFocus ? 1 : 0
            border.color: calendar.accentColor
        }
    }

    contentItem: ColumnLayout {
        spacing: 16 * calendar.uiScale

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 52 * calendar.uiScale
            spacing: 16 * calendar.uiScale

            ToolButton {
                id: monthHeading
                objectName: "monthHeading"
                text: calendar.displayedYear + "年" + calendar.displayedMonth + "月"
                font.pixelSize: 28 * calendar.uiScale
                font.weight: Font.Medium
                hoverEnabled: true
                leftPadding: 12 * calendar.uiScale
                rightPadding: 12 * calendar.uiScale
                Accessible.name: "选择年月，" + text
                onClicked: monthPicker.open()

                contentItem: Row {
                    spacing: 18 * calendar.uiScale
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "▾"
                        color: "#666e79"
                        font.pixelSize: 20 * calendar.uiScale
                    }
                    Text {
                        text: monthHeading.text
                        color: calendar.textColor
                        font: monthHeading.font
                    }
                }
                background: Rectangle {
                    radius: 8
                    color: monthHeading.hovered ? "#e8ebf0" : "transparent"
                    border.width: monthHeading.visualFocus ? 1 : 0
                    border.color: calendar.accentColor
                }
            }

            Item {
                Layout.fillWidth: true
            }

            ArrowButton {
                objectName: "previousMonth"
                enabled: calendar.displayedMonthIndex > calendar.firstMonthIndex
                onClicked: calendar.showMonth(calendar.displayedMonthIndex - 1)
            }

            ArrowButton {
                objectName: "nextMonth"
                forward: true
                enabled: calendar.displayedMonthIndex < calendar.lastMonthIndex
                onClicked: calendar.showMonth(calendar.displayedMonthIndex + 1)
            }
        }

        DayOfWeekRow {
            Layout.fillWidth: true
            Layout.preferredHeight: 44 * calendar.uiScale
            locale: grid.locale
            spacing: 0
            delegate: Text {
                required property int day
                text: ["日", "一", "二", "三", "四", "五", "六"][day]
                color: calendar.mutedColor
                font.pixelSize: 24 * calendar.uiScale
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        MonthGrid {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            locale: Qt.locale("en_US") // Sunday first, matching the reference.
            year: calendar.displayedYear
            month: calendar.displayedMonth - 1
            spacing: 0
            onWidthChanged: calendar.resetIndicator()
            onHeightChanged: calendar.resetIndicator()

            background: LiquidSelection {
                id: liquid
                objectName: "liquidSelection"
                color: calendar.accentColor
                diameter: Math.max(1, Math.min(50 * calendar.uiScale,
                    grid.width / 7 - 4, grid.height / 6 - 4))
                duration: calendar.animationDuration
                stretch: calendar.liquidStretch
                onSettled: {
                    const monthIndex = calendar._pendingMonth;
                    calendar._pendingMonth = -1;
                    if (monthIndex >= 0 && monthIndex !== calendar.displayedMonthIndex)
                        calendar.showMonth(monthIndex);
                }
            }

            delegate: AbstractButton {
                id: dayButton
                required property var model
                readonly property int dateKey: calendar.dayKey(model.date)
                readonly property bool hasData: calendar.dataDates.indexOf(
                    Qt.formatDate(model.date, "yyyy-MM-dd")) !== -1
                readonly property point liquidCenter: grid.contentItem.mapToItem(liquid,
                    x + width / 2, y + height / 2)
                readonly property real glyphHalfWidth: dateLabel.contentWidth / 2
                readonly property real glyphHalfHeight: dateLabel.contentHeight * 0.38
                // A narrow tail can cover a cell's center without covering its
                // digits. Only use white when the whole glyph area is on blue.
                readonly property bool overLiquid: liquid.visible && liquid.motionRevision >= 0
                    && liquid.covers(Qt.point(liquidCenter.x - glyphHalfWidth, liquidCenter.y - glyphHalfHeight))
                    && liquid.covers(Qt.point(liquidCenter.x + glyphHalfWidth, liquidCenter.y - glyphHalfHeight))
                    && liquid.covers(Qt.point(liquidCenter.x - glyphHalfWidth, liquidCenter.y + glyphHalfHeight))
                    && liquid.covers(Qt.point(liquidCenter.x + glyphHalfWidth, liquidCenter.y + glyphHalfHeight))
                objectName: "day-" + Qt.formatDate(model.date, "yyyy-MM-dd")
                enabled: calendar.isSelectable(model.date)
                checked: calendar.dayKey(model.date) === calendar.dayKey(calendar.selectedDate)
                hoverEnabled: enabled
                padding: 0
                Accessible.name: Qt.formatDate(model.date, "yyyy年M月d日")
                Accessible.description: hasData ? "有数据" : ""
                onClicked: calendar.selectDate(model.date)

                contentItem: Text {
                    id: dateLabel
                    text: dayButton.model.day
                    font.pixelSize: 28 * calendar.uiScale
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: dayButton.overLiquid ? "white" : !dayButton.enabled ? "#b9bec7" : dayButton.model.month !== grid.month ? calendar.mutedColor : calendar.textColor

                    Rectangle {
                        objectName: "dataDot-" + Qt.formatDate(dayButton.model.date, "yyyy-MM-dd")
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 20 * calendar.uiScale
                        width: 6 * calendar.uiScale
                        height: width
                        radius: width / 2
                        visible: dayButton.hasData
                        color: calendar.dataDotColor
                        // The dot stays with its date. Only its outline responds
                        // to the liquid passing underneath its own position.
                        border.width: visible && liquid.visible && liquid.motionRevision >= 0
                            && liquid.covers(Qt.point(dayButton.liquidCenter.x,
                                dayButton.liquidCenter.y + anchors.verticalCenterOffset))
                            ? calendar.uiScale : 0
                        border.color: "white"
                    }
                }
                background: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: Math.min(50 * calendar.uiScale, parent.width - 4, parent.height - 4)
                        height: width
                        radius: width / 2
                        color: dayButton.hovered && !dayButton.overLiquid && !liquid.running
                            ? "#e3ebff" : "transparent"
                        border.width: dayButton.visualFocus ? 1 : 0
                        border.color: calendar.accentColor
                    }
                }
            }
        }
    }

    Popup {
        id: monthPicker
        objectName: "monthPicker"
        x: calendar.leftPadding
        y: calendar.topPadding + monthHeading.height + 6
        width: Math.min(340, calendar.width - calendar.leftPadding - calendar.rightPadding)
        padding: 18
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        onOpened: {
            yearSelector.currentIndex = calendar.displayedYear - calendar.minimumYear;
            monthSelector.currentIndex = monthSelector.indexOfValue(calendar.displayedMonth);
        }

        background: Rectangle {
            color: "white"
            radius: 12
            border.color: "#dce0e7"
        }
        Overlay.modal: Item {}

        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                spacing: 12
                ComboBox {
                    id: yearSelector
                    objectName: "yearSelector"
                    Layout.fillWidth: true
                    model: calendar.yearOptions()
                    textRole: "label"
                    valueRole: "value"
                    font.pixelSize: 18
                    Accessible.name: "年份"
                }
                ComboBox {
                    id: monthSelector
                    objectName: "monthSelector"
                    Layout.fillWidth: true
                    model: calendar.monthOptions(yearSelector.currentValue ?? calendar.displayedYear)
                    textRole: "label"
                    valueRole: "value"
                    font.pixelSize: 18
                    Accessible.name: "月份"
                    onModelChanged: currentIndex = Math.max(0, indexOfValue(calendar.displayedMonth))
                }
            }
            Button {
                id: applyMonth
                objectName: "applyMonth"
                Layout.fillWidth: true
                text: "确定"
                font.pixelSize: 18
                enabled: yearSelector.currentIndex >= 0 && monthSelector.currentIndex >= 0
                onClicked: {
                    calendar.showMonth(yearSelector.currentValue * 12 + monthSelector.currentValue - 1);
                    monthPicker.close();
                }
                contentItem: Text {
                    text: applyMonth.text
                    font: applyMonth.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitHeight: 40
                    radius: 6
                    color: calendar.accentColor
                }
            }
        }
    }
}
