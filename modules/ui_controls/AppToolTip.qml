pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Chunfe.Ui

ToolTip {
    id: root
    objectName: "appToolTip"

    required property T.AbstractButton trigger
    readonly property var hostWindow: trigger?.Window.window
    readonly property bool available: !!trigger && trigger.visible && trigger.enabled
                                      && !!hostWindow && hostWindow.visible && triggerBounds.width > 0
                                      && text.length > 0
    readonly property bool keyboardFocused: !!hostWindow && hostWindow.active && !!trigger?.visualFocus
    readonly property rect triggerBounds: visibleTriggerBounds()
    readonly property real gap: 6
    readonly property bool fitsAbove: triggerBounds.y >= height + gap + topMargin
    readonly property bool fitsBelow: !!hostWindow && hostWindow.height - triggerBounds.y
                                     - trigger.height >= height + gap + bottomMargin

    parent: trigger
    popupType: Popup.Item
    focus: false
    modal: false
    closePolicy: Popup.CloseOnPressOutsideParent
    z: 100
    margins: 6
    leftPadding: 10
    rightPadding: 10
    topPadding: 7
    bottomPadding: 7
    width: Math.min(implicitWidth, 280, Math.max(0, hostWindow ? hostWindow.width - 2 * margins : 280))
    timeout: -1

    x: !trigger ? 0 : fitsAbove || fitsBelow ? (trigger.width - width) / 2
       : triggerBounds.x >= width + gap + leftMargin ? -width - gap : trigger.width + gap
    y: !trigger ? 0 : fitsAbove ? -height - gap : fitsBelow ? trigger.height + gap : (trigger.height - height) / 2

    contentItem: Text {
        text: root.text
        textFormat: Text.PlainText
        font.pixelSize: 12
        color: Theme.darkText
        wrapMode: Text.Wrap
        Accessible.ignored: true

        HoverHandler {
            id: tipHover
            onHoveredChanged: {
                if (hovered)
                    hideTimer.stop()
                else
                    root.scheduleHide()
            }
        }
    }

    background: Rectangle {
        color: Theme.darkBackground
        border.color: Theme.darkBorder
        radius: 6
    }

    function visibleTriggerBounds(): rect {
        if (!trigger)
            return Qt.rect(0, 0, 0, 0)

        // Read ancestor offsets so the binding also updates when a Flickable scrolls.
        let x = 0
        let y = 0
        for (let item = trigger; item; item = item.parent) {
            if (item.clip && (x + trigger.width <= 0 || y + trigger.height <= 0
                    || x >= item.width || y >= item.height)) {
                return Qt.rect(0, 0, 0, 0)
            }
            x += item.x
            y += item.y
        }
        return Qt.rect(x, y, trigger.width, trigger.height)
    }

    function schedule() {
        hideTimer.stop()
        if (ToolTipState.current !== root)
            ToolTipState.current?.dismiss()
        ToolTipState.current = root
        if (!visible)
            showTimer.restart()
    }

    function scheduleHide() {
        if (ToolTipState.current === root && !trigger?.hovered && !keyboardFocused && !tipHover.hovered) {
            if (visible)
                hideTimer.restart()
            else
                dismiss()
        }
    }

    function dismiss() {
        showTimer.stop()
        hideTimer.stop()
        close()
        if (ToolTipState.current === root)
            ToolTipState.current = null
    }

    onClosed: {
        if (ToolTipState.current === root)
            ToolTipState.current = null
    }

    onAvailableChanged: {
        if (!available)
            dismiss()
    }
    onKeyboardFocusedChanged: {
        if (keyboardFocused && available)
            schedule()
        else
            scheduleHide()
    }

    Timer {
        id: showTimer
        interval: 500
        onTriggered: {
            if (root.available && (root.trigger.hovered || root.keyboardFocused))
                root.open()
            else
                root.dismiss()
        }
    }
    Timer {
        id: hideTimer
        interval: 240
        onTriggered: root.dismiss()
    }

    Connections {
        target: root.trigger
        function onHoveredChanged() {
            if (root.trigger.hovered && root.available)
                root.schedule()
            else
                root.scheduleHide()
        }
        function onPressed() { root.dismiss() }
    }

    Shortcut {
        sequence: "Escape"
        // The tooltip lives in the overlay beside the dialog, outside its shortcut scope.
        context: Qt.ApplicationShortcut
        enabled: root.opened && !!root.hostWindow?.active
        autoRepeat: false
        onActivated: root.dismiss()
    }
}
