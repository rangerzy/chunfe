import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Calendar

TestCase {
    name: "Calendar"
    when: windowShown
    width: 554
    height: 588

    property var appWindow
    property var calendar

    Component {
        id: defaultCalendarFactory
        PastDateCalendar {}
    }

    Component {
        id: windowFactory

        ApplicationWindow {
            width: 492
            height: 532
            minimumWidth: 320
            minimumHeight: 360
            visible: true
            font.family: "Helvetica Neue"

            PastDateCalendar {
                objectName: "calendar"
                anchors.fill: parent
                minimumYear: 2026
                minimumMonth: 10
                today: new Date(2026, 9, 26, 12)
                selectedDate: new Date(2026, 9, 26)
                displayedYear: 2026
                displayedMonth: 10
                onMonthChanged: (year, month) => {
                    dataDates = year === 2026 && month === 9 ? ["2026-09-23"] : []
                }
            }
        }
    }

    SignalSpy {
        id: selectionSpy
        signalName: "dateSelected"
    }

    SignalSpy {
        id: monthSpy
        signalName: "monthChanged"
    }

    function init() {
        appWindow = windowFactory.createObject(null)
        verify(appWindow !== null)
        calendar = findChild(appWindow, "calendar")
        verify(calendar !== null, "The calendar must provide date selection and navigation")
        selectionSpy.target = calendar
        selectionSpy.clear()
        monthSpy.target = calendar
        monthSpy.clear()
        verify(monthSpy.valid, "The calendar must notify consumers of the displayed month")
        waitForRendering(calendar)
        tryCompare(liquid(), "visible", true)
        tryCompare(monthSpy, "count", 1)
        compare(notifiedMonths(), [[2026, 10]])
        monthSpy.clear()
    }

    function cleanup() {
        if (appWindow)
            appWindow.destroy()
    }

    function day(isoDate) {
        const item = findChild(calendar, "day-" + isoDate)
        verify(item !== null, "Missing date: " + isoDate)
        return item
    }

    function selected() {
        return Qt.formatDate(calendar.selectedDate, "yyyy-MM-dd")
    }

    function notifiedMonths() {
        // SignalSpy stores each emission as an Arguments object, not an Array.
        return monthSpy.signalArguments.map(args => [args[0], args[1]])
    }

    function liquid() {
        const indicator = findChild(calendar, "liquidSelection")
        verify(indicator !== null, "Selection should have a shared liquid background")
        return indicator
    }

    function center(isoDate) {
        const item = day(isoDate)
        return item.mapToItem(liquid(), item.width / 2, item.height / 2)
    }

    function atDate(isoDate) {
        const position = center(isoDate)
        fuzzyCompare(liquid().centerX, position.x, 0.5)
        fuzzyCompare(liquid().centerY, position.y, 0.5)
    }

    function test_initial_month_and_selection() {
        compare(findChild(calendar, "monthHeading").text, "2026年10月")
        verify(day("2026-10-26").checked)
        atDate("2026-10-26")
        compare(day("2026-10-26").contentItem.color, "#ffffff")
        // Six complete weeks, Sunday first, including both adjacent months.
        verify(day("2026-09-27") !== null)
        verify(day("2026-11-07") !== null)
        compare(calendar.dataDates.length, 0)
    }

    function test_default_minimum_month_starts_in_configured_current_year() {
        const defaults = createTemporaryObject(defaultCalendarFactory, appWindow.contentItem, {
            visible: false,
            today: new Date(2024, 1, 29)
        })
        verify(defaults !== null)
        compare(defaults.minimumYear, 2024)
        compare(defaults.minimumMonth, 1)
        defaults.today = new Date(2027, 0, 5)
        compare(defaults.minimumYear, 2027)
        compare(defaults.minimumMonth, 1)
    }

    function test_data_dates_can_be_replaced_without_changing_selection() {
        calendar.dataDates = ["2026-10-12", "2026-10-25"]
        calendar.dataDotColor = "#35a56a"
        verify(findChild(calendar, "dataDot-2026-10-12").visible)
        verify(findChild(calendar, "dataDot-2026-10-25").visible)
        verify(!findChild(calendar, "dataDot-2026-10-26").visible)
        compare(findChild(calendar, "dataDot-2026-10-12").color, "#35a56a")
        compare(selected(), "2026-10-26")
        compare(selectionSpy.count, 0)

        calendar.dataDates = ["2026-10-26"]
        verify(!findChild(calendar, "dataDot-2026-10-12").visible)
        verify(!findChild(calendar, "dataDot-2026-10-25").visible)
        verify(findChild(calendar, "dataDot-2026-10-26").visible)
        compare(selected(), "2026-10-26")

        calendar.dataDates = []
        verify(!findChild(calendar, "dataDot-2026-10-26").visible)
        compare(selectionSpy.count, 0)
    }

    function test_instances_keep_selection_navigation_and_markers_independent() {
        const otherWindow = createTemporaryObject(windowFactory, null)
        verify(otherWindow !== null)
        const other = findChild(otherWindow, "calendar")
        verify(other !== null)
        waitForRendering(other)

        calendar.minimumMonth = 9
        calendar.selectDate(new Date(2026, 9, 12))
        calendar.showMonth(2026 * 12 + 8)
        calendar.dataDates = ["2026-09-21"]
        compare(selected(), "2026-10-12")
        compare(calendar.displayedMonth, 9)
        verify(findChild(calendar, "dataDot-2026-09-21").visible)
        compare(Qt.formatDate(other.selectedDate, "yyyy-MM-dd"), "2026-10-26")
        compare(other.displayedMonth, 10)
        compare(other.minimumMonth, 10)
        compare(other.dataDates.length, 0)

        other.selectDate(new Date(2026, 9, 20))
        other.dataDates = ["2026-10-20"]
        verify(findChild(other, "dataDot-2026-10-20").visible)
        compare(selected(), "2026-10-12")
        compare(calendar.displayedMonth, 9)
        compare(calendar.dataDates.length, 1)
        compare(calendar.dataDates[0], "2026-09-21")
        compare(selectionSpy.count, 1)
    }

    function test_bounds_block_navigation_and_dates() {
        verify(!findChild(calendar, "previousMonth").enabled)
        verify(!findChild(calendar, "nextMonth").enabled)
        verify(!day("2026-09-30").enabled)
        verify(!day("2026-10-27").enabled)
        verify(day("2026-10-01").enabled)
        verify(day("2026-10-26").enabled)
        mouseClick(day("2026-10-27"))
        compare(selected(), "2026-10-26")
        mouseClick(day("2026-09-30"))
        compare(selected(), "2026-10-26")
        calendar.showMonth(calendar.firstMonthIndex - 1)
        calendar.showMonth(calendar.lastMonthIndex + 1)
        wait(0)
        compare(monthSpy.count, 0)
    }

    function test_click_selects_and_logs_each_time() {
        mouseClick(day("2026-10-12"))
        compare(selected(), "2026-10-12")
        verify(day("2026-10-12").checked)
        verify(!day("2026-10-26").checked)
        mouseClick(day("2026-10-12"))
        compare(selected(), "2026-10-12")
        compare(selectionSpy.count, 2)
        compare(Qt.formatDate(selectionSpy.signalArguments[0][0], "yyyy-MM-dd"), "2026-10-12")
    }

    function test_previous_month_preserves_selection() {
        calendar.minimumMonth = 9
        mouseClick(findChild(calendar, "previousMonth"))
        compare(findChild(calendar, "monthHeading").text, "2026年9月")
        compare(notifiedMonths(), [[2026, 9]])
        compare(calendar.dataDates.length, 1)
        compare(calendar.dataDates[0], "2026-09-23")
        verify(findChild(calendar, "dataDot-2026-09-23").visible)
        compare(selected(), "2026-10-26")
        verify(!findChild(calendar, "previousMonth").enabled)
        verify(findChild(calendar, "nextMonth").enabled)
        mouseClick(findChild(calendar, "nextMonth"))
        compare(findChild(calendar, "monthHeading").text, "2026年10月")
        verify(day("2026-10-26").checked)
        compare(notifiedMonths(), [[2026, 9], [2026, 10]])
        compare(calendar.dataDates.length, 0)
        wait(0)
        compare(monthSpy.count, 2)
    }

    function test_adjacent_month_click_switches_month() {
        calendar.minimumMonth = 9
        mouseClick(day("2026-09-30"))
        compare(selected(), "2026-09-30")
        compare(findChild(calendar, "monthHeading").text, "2026年10月")
        verify(liquid().running)
        compare(monthSpy.count, 0)
        tryCompare(calendar, "displayedMonth", 9)
        compare(notifiedMonths(), [[2026, 9]])
        verify(findChild(calendar, "dataDot-2026-09-23").visible)
        verify(day("2026-09-30").checked)
        tryCompare(liquid(), "running", false)
        tryCompare(liquid(), "visible", true)
        atDate("2026-09-30")
    }

    function test_flow_keeps_selection_immediate_and_arrives_at_target() {
        const source = center("2026-10-26")
        const destination = center("2026-10-01")
        mouseClick(day("2026-10-01"))
        compare(selected(), "2026-10-01")
        compare(selectionSpy.count, 1)
        verify(liquid().running)
        verify(Math.abs(liquid().centerY - source.y) < Math.abs(destination.y - source.y) / 2)
        tryVerify(function() { return liquid().deformation > 0.1 })
        verify(liquid().centerY > destination.y)
        tryCompare(liquid(), "running", false)
        atDate("2026-10-01")
        fuzzyCompare(liquid().deformation, 0, 0.001)
    }

    function test_fast_click_retargets_without_restarting_from_old_date() {
        calendar.animationDuration = 900
        mouseClick(day("2026-10-01"))
        tryVerify(function() { return liquid().progress > 0.2 })
        const x = liquid().centerX
        const y = liquid().centerY
        mouseClick(day("2026-10-24"))
        fuzzyCompare(liquid().centerX, x, 5)
        fuzzyCompare(liquid().centerY, y, 5)
        compare(selected(), "2026-10-24")
        compare(selectionSpy.count, 2)
        const progress = liquid().progress
        mouseClick(day("2026-10-24"))
        verify(liquid().progress >= progress)
        compare(selectionSpy.count, 3)
        tryCompare(liquid(), "running", false)
        atDate("2026-10-24")
    }

    function test_new_target_cancels_pending_adjacent_month_switch() {
        calendar.minimumMonth = 9
        mouseClick(day("2026-09-30"))
        verify(liquid().running)
        mouseClick(day("2026-10-12"))
        tryCompare(liquid(), "running", false)
        compare(calendar.displayedMonth, 10)
        compare(selected(), "2026-10-12")
        atDate("2026-10-12")
        compare(monthSpy.count, 0)
    }

    function test_manual_navigation_cancels_flow_and_hides_absent_selection() {
        calendar.minimumMonth = 9
        mouseClick(day("2026-10-12"))
        verify(liquid().running)
        mouseClick(findChild(calendar, "previousMonth"))
        compare(calendar.displayedMonth, 9)
        tryCompare(liquid(), "running", false)
        tryCompare(liquid(), "visible", false)
        compare(selected(), "2026-10-12")
        mouseClick(findChild(calendar, "nextMonth"))
        tryCompare(liquid(), "visible", true)
        atDate("2026-10-12")
    }

    function test_hidden_source_gathers_at_new_date() {
        calendar.minimumMonth = 9
        mouseClick(findChild(calendar, "previousMonth"))
        tryCompare(liquid(), "visible", false)
        mouseClick(day("2026-09-15"))
        compare(selected(), "2026-09-15")
        verify(liquid().running)
        atDate("2026-09-15")
        tryCompare(liquid(), "running", false)
        atDate("2026-09-15")
    }

    function test_resize_during_flow_settles_on_selected_date() {
        mouseClick(day("2026-10-12"))
        verify(liquid().running)
        appWindow.width = 320
        appWindow.height = 360
        tryCompare(liquid(), "running", false)
        waitForRendering(calendar)
        atDate("2026-10-12")
    }

    function test_digit_contrast_follows_indicator_without_a_render_delay() {
        // Selection remains on the 26th: text contrast must follow the moving
        // blue geometry, not the selected date or the previous rendered frame.
        const indicator = liquid()
        for (const date of ["2026-10-20", "2026-10-21", "2026-10-19", "2026-10-20"]) {
            indicator.snapTo(center(date))
            compare(day(date).contentItem.color, "#ffffff")
            for (const other of ["2026-10-19", "2026-10-20", "2026-10-21", "2026-10-26"]) {
                if (other !== date)
                    compare(day(other).contentItem.color, calendar.textColor)
            }
        }
    }

    function test_horizontal_rapid_clicks_keep_latest_target_and_short_tail() {
        mouseClick(day("2026-10-20"))
        tryCompare(liquid(), "running", false)
        const rowCenter = center("2026-10-20").y
        for (let i = 0; i < 10; ++i) {
            const date = i % 2 === 0 ? "2026-10-21" : "2026-10-19"
            mouseClick(day(date))
            wait(45)
            const indicator = liquid()
            verify(indicator.deformation <= 1.001,
                   "Repeated clicking must not pump up the liquid deformation")
            fuzzyCompare(indicator.centerY, rowCenter, 0.5)
            verify(!indicator.covers(Qt.point(indicator.centerX, rowCenter - 30)),
                   "A horizontal reversal must not sweep a tail toward the previous week")
            verify(!indicator.covers(Qt.point(indicator.centerX, rowCenter + 30)))
        }
        compare(selected(), "2026-10-19")
        compare(selectionSpy.count, 11)
        tryCompare(liquid(), "running", false, 1000)
        atDate("2026-10-19")
        compare(day("2026-10-19").contentItem.color, "#ffffff")
    }

    function test_selection_handler_can_replace_pending_cross_month_target() {
        calendar.minimumMonth = 9
        const handler = function(date) {
            if (Qt.formatDate(date, "yyyy-MM-dd") === "2026-09-30")
                calendar.selectDate(new Date(2026, 9, 12))
        }
        calendar.dateSelected.connect(handler)
        mouseClick(day("2026-09-30"))
        calendar.dateSelected.disconnect(handler)
        tryCompare(liquid(), "running", false)
        compare(selected(), "2026-10-12")
        compare(calendar.displayedMonth, 10)
        atDate("2026-10-12")
    }

    function test_selection_handler_can_navigate_without_being_overridden() {
        calendar.minimumMonth = 9
        const handler = function(date) { calendar.showMonth(2026 * 12 + 8) }
        calendar.dateSelected.connect(handler)
        mouseClick(day("2026-10-12"))
        calendar.dateSelected.disconnect(handler)
        compare(calendar.displayedMonth, 9)
        tryCompare(liquid(), "running", false)
        tryCompare(liquid(), "visible", false)
        compare(selected(), "2026-10-12")
    }

    function test_year_boundary_navigation() {
        calendar.minimumYear = 2025
        calendar.minimumMonth = 12
        calendar.today = new Date(2027, 0, 5)
        calendar.displayedYear = 2027
        calendar.displayedMonth = 1
        tryCompare(monthSpy, "count", 1)
        compare(notifiedMonths(), [[2027, 1]])
        monthSpy.clear()
        mouseClick(findChild(calendar, "previousMonth"))
        compare(findChild(calendar, "monthHeading").text, "2026年12月")
        mouseClick(findChild(calendar, "nextMonth"))
        compare(findChild(calendar, "monthHeading").text, "2027年1月")
        verify(!findChild(calendar, "nextMonth").enabled)
        compare(notifiedMonths(), [[2026, 12], [2027, 1]])
        wait(0)
        compare(monthSpy.count, 2)
    }

    function test_picker_limits_and_applies_month() {
        calendar.minimumYear = 2025
        calendar.minimumMonth = 11
        mouseClick(findChild(calendar, "monthHeading"))
        const popup = findChild(calendar, "monthPicker")
        tryCompare(popup, "opened", true)
        const years = findChild(calendar, "yearSelector")
        const months = findChild(calendar, "monthSelector")
        compare(years.count, 2)
        compare(years.textAt(0), "2025年")
        compare(years.textAt(1), "2026年")
        compare(months.count, 10)
        years.currentIndex = 0
        compare(months.count, 2)
        compare(months.textAt(0), "11月")
        compare(months.textAt(1), "12月")
        months.currentIndex = 1
        mouseClick(findChild(calendar, "applyMonth"))
        compare(findChild(calendar, "monthHeading").text, "2025年12月")
        compare(selected(), "2026-10-26")
        compare(notifiedMonths(), [[2025, 12]])
        wait(0)
        compare(monthSpy.count, 1)
    }

    function test_same_month_confirmation_cancels_pending_switch_without_notification() {
        calendar.minimumMonth = 9
        calendar.animationDuration = 1500
        mouseClick(day("2026-09-30"))
        verify(liquid().running)
        mouseClick(findChild(calendar, "monthHeading"))
        tryCompare(findChild(calendar, "monthPicker"), "opened", true)
        mouseClick(findChild(calendar, "applyMonth"))
        tryCompare(liquid(), "running", false)
        compare(calendar.displayedMonth, 10)
        wait(0)
        compare(monthSpy.count, 0)
    }

    function test_month_handler_can_navigate_again_without_duplicate_notifications() {
        calendar.minimumMonth = 9
        const handler = function(year, month) {
            if (year === 2026 && month === 9)
                calendar.showMonth(2026 * 12 + 9)
        }
        calendar.monthChanged.connect(handler)
        mouseClick(findChild(calendar, "previousMonth"))
        calendar.monthChanged.disconnect(handler)
        compare(calendar.displayedMonth, 10)
        compare(notifiedMonths(), [[2026, 9], [2026, 10]])
        compare(calendar.dataDates.length, 0)
        wait(0)
        compare(monthSpy.count, 2)
    }

    function test_hover_only_on_available_days() {
        mouseMove(day("2026-10-12"))
        tryCompare(day("2026-10-12"), "hovered", true)
        mouseMove(day("2026-10-27"))
        verify(!day("2026-10-27").hovered)
        compare(selected(), "2026-10-26")
    }

    function test_leap_day_and_today_without_time_of_day() {
        calendar.minimumYear = 2024
        calendar.minimumMonth = 2
        calendar.today = new Date(2024, 1, 29, 0, 1)
        calendar.displayedYear = 2024
        calendar.displayedMonth = 2
        verify(day("2024-02-29").enabled)
        verify(!day("2024-03-01").enabled)
        mouseClick(day("2024-02-29"))
        compare(selected(), "2024-02-29")
    }
}
