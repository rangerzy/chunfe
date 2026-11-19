import QtQuick
import QtTest
import Chunfe

TestCase {
    name: "Notifications"
    when: windowShown

    property var app

    Component {
        id: backgroundFactory
        QtObject { property string backgroundText: "" }
    }

    Component {
        id: factory
        Main {
            modelConnection: QtObject {
                readonly property string apiEndpoint: "https://api.deepseek.com"
                readonly property var availableModels: ["deepseek-flash", "deepseek-v4-pro"]
                property string savedModelName: "deepseek-flash"
                property string savedApiKey: ""
                property string draftModelName: "deepseek-flash"
                property string draftApiKey: ""
                readonly property bool modelConfigured: savedApiKey.length > 0
                readonly property bool configurationDirty: draftModelName !== savedModelName || draftApiKey.trim() !== savedApiKey
                property string verificationStatus: "unconfigured"
                property string verificationMessage: ""
                property bool verificationRestored: false
                property int verificationCount: 0
                property bool saveFails: false

                signal notification(string message, string severity)

                function testConnection() {
                    if (!modelConfigured || verificationStatus === "testing")
                        return false
                    verificationCount++
                    verificationMessage = ""
                    verificationStatus = "testing"
                    return true
                }

                function saveModel() {
                    if (!configurationDirty)
                        return true
                    if (saveFails) {
                        notification("保存模型配置失败，请检查配置目录的访问权限。", "error")
                        return false
                    }
                    const key = draftApiKey.trim()
                    savedModelName = draftModelName
                    if (key !== savedApiKey) {
                        savedApiKey = key
                        verificationMessage = ""
                        verificationStatus = modelConfigured ? "untested" : "unconfigured"
                        if (modelConfigured)
                            testConnection()
                    }
                    return true
                }
            }
        }
    }

    function init() {
        failOnWarning(/Binding loop detected|invalid context|exception occurred during delayed function evaluation/)
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

    function startModelCheck() {
        const page = openModelPage()
        app.modelConnection.draftApiKey = "test-key"
        clickModelAction(page, "testConnectionButton")
        compare(app.modelConnection.verificationStatus, "testing")
        return page
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
        const key = findChild(page, "apiKeyField")
        verify(key !== null)
        key.forceActiveFocus()
        tryCompare(key, "activeFocus", true)

        const toast = notificationToast()
        app.demoController.notify("连接失败。", "error")
        tryCompare(toast, "visible", true)
        const shortHeight = toast.height
        const message = "无法连接到模型服务。请确认 API Key 有权访问当前模型，并核对模型名称。"
                + "如果配置无误，请检查网络连接，稍后返回模型连接页重新测试。"
        app.demoController.notify(message, "error")
        tryCompare(toast, "message", message)
        tryVerify(function () { return toast.height > shortHeight })
        compare(toast.severity, "error")
        verify(key.activeFocus)

        const topLeft = toast.mapToItem(page, 0, 0)
        const bottomRight = toast.mapToItem(page, toast.width, toast.height)
        verify(topLeft.x >= 0 && topLeft.y >= 0)
        verify(bottomRight.x <= page.width && bottomRight.y <= page.height)

        savePreview(toast, "notification-error-minimum.png")
    }

    function test_leavingModelPageSavesDirtyConfiguration() {
        const page = openModelPage()
        const key = findChild(page, "apiKeyField")
        verify(key !== null)
        key.forceActiveFocus()
        tryCompare(key, "activeFocus", true)
        app.modelConnection.draftApiKey = "test-key"
        compare(app.modelConnection.savedApiKey, "")
        app.demoController.route = "home"
        tryCompare(app.modelConnection, "savedApiKey", "test-key")
        compare(app.modelConnection.verificationCount, 1)
        verifyHomeStatus("modelHomeStatus", "模型检测中")
        verify(!notificationToast().visible)
    }

    function verifyHomeStatus(name, expected) {
        tryVerify(function() {
            const statuses = findChild(app, "homeStatusItems")
            if (!statuses)
                return false
            for (let index = 0; index < statuses.count; ++index) {
                const status = statuses.itemAt(index)
                if (status?.objectName === name)
                    return status.modelData.value === expected
            }
            return false
        }, 1500, "Expected " + name + " to display " + expected)
    }

    function test_homeStatusReflectsSavedBackgroundAndActualReadiness() {
        compare(app.demoController.engineStatus, "ready")
        verifyHomeStatus("modelHomeStatus", "模型未配置")
        verifyHomeStatus("audioHomeStatus", "转写未就绪")
        verifyHomeStatus("backgroundHomeStatus", "未设置背景")

        app.modelConnection.draftApiKey = "test-key"
        verifyHomeStatus("modelHomeStatus", "模型未配置")
        app.modelConnection.saveModel()
        verifyHomeStatus("modelHomeStatus", "模型检测中")
        app.modelConnection.verificationStatus = "untested"
        verifyHomeStatus("modelHomeStatus", "模型待检测")
        app.modelConnection.verificationStatus = "passed"
        verifyHomeStatus("modelHomeStatus", "API 连接正常")
        app.modelConnection.verificationStatus = "failed"
        verifyHomeStatus("modelHomeStatus", "模型检测失败")
        app.modelConnection.draftApiKey = ""
        app.modelConnection.saveModel()
        verifyHomeStatus("modelHomeStatus", "模型未配置")

        const offline = app.demoController.transcriptionTest
        offline.engineStatus = "stopped"
        app.offlineTranscription = offline
        verifyHomeStatus("audioHomeStatus", "转写未就绪")
        offline.engineStatus = "starting"
        verifyHomeStatus("audioHomeStatus", "转写加载中")
        offline.engineStatus = "ready"
        verifyHomeStatus("audioHomeStatus", "转写就绪")
        for (const state of ["stopping", "error", "stopped"]) {
            offline.engineStatus = state
            verifyHomeStatus("audioHomeStatus", "转写未就绪")
        }

        const background = createTemporaryObject(backgroundFactory, app)
        app.backgroundDocument = background
        background.backgroundText = "# 已导入的背景\n\n- 原文"
        verifyHomeStatus("backgroundHomeStatus", "背景已配置")
        compare(app.demoController.backgroundText, background.backgroundText)
        background.backgroundText = ""
        verifyHomeStatus("backgroundHomeStatus", "未设置背景")
    }

    function test_connectionErrorStaysInline() {
        const page = startModelCheck()
        const toast = notificationToast()
        verify(!toast.visible, "Testing the connection should not show a save notification")

        app.modelConnection.verificationMessage = "DeepSeek API Key 无效，请检查后重试。"
        app.modelConnection.verificationStatus = "failed"
        const status = findChild(page, "modelVerificationStatus")
        verify(status !== null)
        tryCompare(status, "text", "DeepSeek API Key 无效，请检查后重试。")
        verify(status.visible)
        verify(!toast.visible, "The inline result should not be duplicated by a notification")
    }

    function test_restoredModelCheckAppearsWithoutSendingAnotherRequest() {
        app.modelConnection.savedApiKey = "test-key"
        app.modelConnection.draftApiKey = "test-key"
        app.modelConnection.verificationStatus = "passed"
        app.modelConnection.verificationRestored = true
        verifyHomeStatus("modelHomeStatus", "上次检测通过")

        const page = openModelPage()
        const status = findChild(page, "modelVerificationStatus")
        compare(status.text, "上次检测通过")
        app.modelConnection.verificationMessage = "无法连接到 DeepSeek，请检查网络后重试。"
        app.modelConnection.verificationStatus = "failed"
        compare(status.text, "上次检测失败。无法连接到 DeepSeek，请检查网络后重试。")

        app.demoController.route = "home"
        verifyHomeStatus("modelHomeStatus", "上次检测失败")
        compare(app.modelConnection.verificationCount, 0)
    }

    function test_modelCheckCompletesAfterLeavingPage() {
        startModelCheck()
        app.demoController.route = "home"
        verifyHomeStatus("modelHomeStatus", "模型检测中")
        compare(app.modelConnection.verificationCount, 1)

        app.modelConnection.verificationStatus = "passed"
        verifyHomeStatus("modelHomeStatus", "API 连接正常")
        verify(!notificationToast().visible)
        const page = openModelPage()
        compare(findChild(page, "modelVerificationStatus").text, "API 连接正常")
        compare(app.modelConnection.verificationCount, 1)
    }

    function test_modelSaveFailureShowsErrorAndKeepsDraft() {
        const page = openModelPage()
        app.modelConnection.saveFails = true
        app.modelConnection.draftApiKey = "test-key"
        clickModelAction(page, "testConnectionButton")
        const toast = notificationToast()
        tryCompare(toast, "visible", true)
        compare(toast.message, "保存模型配置失败，请检查配置目录的访问权限。")
        compare(toast.severity, "error")
        compare(app.modelConnection.savedApiKey, "")
        compare(app.modelConnection.draftApiKey, "test-key")
        compare(app.modelConnection.verificationCount, 0)
    }
}
