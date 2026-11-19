import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Chunfe.Ui

Item {
    id: scene
    width: 320
    height: 220

    IconButton {
        id: firstButton
        x: 2
        y: 2
        iconName: "settings"
        text: "路径：/模型文件/very-long-directory-name-without-spaces/离线转写模型文件.gguf"
    }
    IconButton {
        id: secondButton
        x: 150
        y: 110
        iconName: "close"
        text: "关闭"
    }
    Dialog {
        id: dialog
        anchors.centerIn: parent
        modal: true
        contentItem: Row {
            IconButton {
                id: dialogButton
                iconName: "close"
                text: "关闭弹窗"
            }
            TextField { id: dialogField; width: 100 }
        }
    }
    Flickable {
        id: scroller
        x: 45
        y: 95
        width: 85
        height: 100
        clip: true
        contentHeight: 280
        IconButton {
            id: scrollButton
            iconName: "settings"
            text: "配置"
        }
    }

    TestCase {
        name: "AppToolTips"
        when: windowShown

        function init() {
            failOnWarning(/Binding loop detected/)
            scene.forceActiveFocus()
            mouseMove(scene, 310, 210)
            scroller.contentY = 0
        }

        function cleanup() {
            dialog.close()
            scene.forceActiveFocus()
            mouseMove(scene, 310, 210)
        }

        function test_hover_tip_wraps_at_edge_and_dismisses_on_press() {
            mouseMove(firstButton, 16, 16)
            const tip = findChild(firstButton, "appToolTip")
            verify(tip)
            tryCompare(tip, "opened", true)
            const topLeft = firstButton.mapToItem(scene, tip.x, tip.y)
            verify(topLeft.x >= 0)
            verify(topLeft.x + tip.width <= scene.width)
            verify(topLeft.y >= firstButton.y + firstButton.height)
            verify(tip.contentItem.lineCount > 1)
            if (typeof testArtifactDirectory !== "undefined" && testArtifactDirectory) {
                waitForRendering(tip.contentItem)
                grabImage(scene.Window.window.contentItem).save(testArtifactDirectory + "/tooltip-edge.png")
            }
            mouseClick(firstButton)
            tryCompare(tip, "opened", false)
        }

        function test_hover_tip_replaces_keyboard_tip() {
            firstButton.forceActiveFocus(Qt.TabFocusReason)
            const firstTip = findChild(firstButton, "appToolTip")
            verify(firstTip)
            tryCompare(firstTip, "opened", true)
            mouseMove(secondButton, 16, 16)
            const secondTip = findChild(secondButton, "appToolTip")
            verify(secondTip)
            tryCompare(secondTip, "opened", true)
            compare(firstTip.opened, false)
            mouseMove(scene, 310, 210)
            tryCompare(secondTip, "opened", false)
        }

        function test_pointer_can_move_onto_tip_across_gap() {
            mouseMove(firstButton, 16, 16)
            const tip = findChild(firstButton, "appToolTip")
            verify(tip)
            tryCompare(tip, "opened", true)
            const topLeft = firstButton.mapToItem(scene, tip.x, tip.y)
            mouseMove(scene, topLeft.x + 18, topLeft.y - 3)
            wait(50)
            mouseMove(scene, topLeft.x + 18, topLeft.y + 12)
            wait(300)
            verify(tip.opened)
            mouseMove(scene, 310, 210)
            tryCompare(tip, "opened", false)
        }

        function test_scroll_hides_tip_when_focused_trigger_leaves_viewport() {
            scrollButton.forceActiveFocus(Qt.TabFocusReason)
            const tip = findChild(scrollButton, "appToolTip")
            verify(tip)
            tryCompare(tip, "opened", true)
            scroller.contentY = 90
            verify(scrollButton.visualFocus)
            tryCompare(tip, "opened", false, 1000)
        }

        function test_escape_dismisses_tooltip_before_modal_dialog() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            dialogButton.forceActiveFocus(Qt.TabFocusReason)
            const tip = findChild(dialogButton, "appToolTip")
            verify(tip)
            tryCompare(tip, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(tip, "opened", false)
            compare(dialog.opened, true)
            keyClick(Qt.Key_Escape)
            tryCompare(dialog, "opened", false)
        }

        function test_escape_dismisses_hovered_tooltip_with_focus_elsewhere_in_dialog() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            dialogField.forceActiveFocus(Qt.TabFocusReason)
            mouseMove(dialogButton, 16, 16)
            const tip = findChild(dialogButton, "appToolTip")
            verify(tip)
            tryCompare(tip, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(tip, "opened", false)
            verify(dialog.opened)
            verify(dialogField.activeFocus)
        }
    }
}
