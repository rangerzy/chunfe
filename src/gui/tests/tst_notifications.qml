import QtQuick
import QtTest
import Chunfe

TestCase {
    name: "Notifications"
    when: windowShown

    property var app

    Component { id: factory; Main {} }

    function init() {
        failOnWarning(/Binding loop detected/)
        app = createTemporaryObject(factory, null)
        verify(app !== null)
    }

    function notificationToast() {
        const toast = findChild(app, "notificationToast")
        verify(toast !== null, "The main window should have a notification surface")
        return toast
    }

    function openModelPage() {
        app.demoController.route = "model"
        const loader = findChild(app, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        return loader.item
    }

    function clickModelAction(page, name) {
        const button = findChild(page, name)
        verify(button !== null)
        page.contentY = Math.max(0, page.contentHeight - page.height)
        mouseClick(button)
    }

    function savePreview(toast, fileName) {
        if (!testArtifactDirectory)
            return
        tryCompare(toast, "opacity", 1)
        wait(80)
        verify(toast.visible)
        grabImage(app.contentItem).save(testArtifactDirectory + "/" + fileName)
    }

    function test_replacementStartsItsOwnTimeout() {
        const toast = notificationToast()
        verify(!toast.visible)
        app.demoController.notify("正在准备资料。", "info")
        tryCompare(toast, "visible", true)
        compare(toast.severity, "info")

        wait(1200)
        app.demoController.notify("资料已保存。", "success")
        tryCompare(toast, "message", "资料已保存。")
        verify(toast.visible)
        compare(toast.severity, "success")

        wait(3200)
        verify(toast.visible, "The previous notification's deadline should not hide its replacement")
        tryCompare(toast, "visible", false, 2000)
    }

    function test_longErrorStaysInsideThePageAndKeepsKeyboardFocus() {
        app.width = app.minimumWidth
        app.height = app.minimumHeight
        const page = openModelPage()
        const endpoint = findChild(page, "endpointField")
        verify(endpoint !== null)
        endpoint.forceActiveFocus()
        tryCompare(endpoint, "activeFocus", true)

        const toast = notificationToast()
        app.demoController.notify("连接失败。", "error")
        tryCompare(toast, "visible", true)
        const shortHeight = toast.height
        const message = "无法连接到模型服务。请检查 API 地址是否包含正确的版本路径，"
                + "确认 API Key 有权访问当前模型，并核对模型名称。"
                + "如果配置无误，请检查网络连接后重新测试。"
        app.demoController.notify(message, "error")
        tryCompare(toast, "message", message)
        tryVerify(function () { return toast.height > shortHeight })
        compare(toast.severity, "error")
        verify(endpoint.activeFocus)

        const topLeft = toast.mapToItem(page, 0, 0)
        const bottomRight = toast.mapToItem(page, toast.width, toast.height)
        verify(topLeft.x >= 0 && topLeft.y >= 0)
        verify(bottomRight.x <= page.width && bottomRight.y <= page.height)

        savePreview(toast, "notification-error-minimum.png")
    }

    function test_explicitModelSaveShowsSuccess() {
        const page = openModelPage()
        const model = findChild(page, "modelNameField")
        verify(model !== null)
        model.text = "test-saved-model"
        clickModelAction(page, "saveModelButton")
        compare(app.demoController.modelName, "test-saved-model")
        const toast = notificationToast()
        tryCompare(toast, "visible", true)
        compare(toast.message, "模型配置已保存。")
        compare(toast.severity, "success")

        savePreview(toast, "notification-save-success.png")
    }

    function test_connectionUsesInlineStatusWhileModelPageIsOpen() {
        const page = openModelPage()
        const model = findChild(page, "modelNameField")
        verify(model !== null)
        model.text = "test-connection-model"
        clickModelAction(page, "testConnectionButton")
        compare(app.demoController.connectionStatus, "testing")
        compare(app.demoController.modelName, "test-connection-model")
        const toast = notificationToast()
        verify(!toast.visible, "Testing the connection should not show a save notification")

        tryCompare(app.demoController, "connectionStatus", "connected")
        verify(!toast.visible, "The inline result should not be duplicated by a notification")
    }

    function test_connectionResultFollowsNavigation() {
        const page = openModelPage()
        clickModelAction(page, "testConnectionButton")
        compare(app.demoController.connectionStatus, "testing")
        mouseClick(findChild(app, "navHome"))
        compare(app.demoController.route, "home")

        const toast = notificationToast()
        tryCompare(toast, "visible", true)
        compare(app.demoController.connectionStatus, "connected")
        compare(toast.message, "连接成功。")
        compare(toast.severity, "success")
    }
}
