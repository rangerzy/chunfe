import QtQuick
import QtTest
import Chunfe

TestCase {
    id: testCase
    name: "NativeWindows"
    when: windowShown
    QtObject {
        id: fakeAudioDevices
        property var systemDevices: []
        property var microphoneDevices: []
        property int refreshCount: 0
        function refresh() { refreshCount++ }
    }
    QtObject {
        id: fakeAudioLevelTest
        property bool active: false
        property bool stopping: false
        property real microphoneLevel: 0
        property real systemLevel: 0
        property string microphoneStatus: ""
        property string systemStatus: ""
        property int starts: 0
        property int stops: 0
        property int deviceUpdates: 0
        property string microphoneId: ""
        property string systemId: ""
        function start(mic, system) {
            starts++
            microphoneId = mic
            systemId = system
            active = true
        }
        function stop() {
            if (active) {
                stops++
                stopping = true
            }
            active = false
            microphoneLevel = 0
            systemLevel = 0
        }
        function finishStop() { stopping = false }
        function setDevices(mic, system) {
            deviceUpdates++
            if (active && (microphoneId !== mic || systemId !== system))
                stop()
            microphoneId = mic
            systemId = system
        }
        function reset() {
            active = false
            stopping = false
            microphoneLevel = 0
            systemLevel = 0
            microphoneStatus = ""
            systemStatus = ""
            starts = 0
            stops = 0
            deviceUpdates = 0
            microphoneId = ""
            systemId = ""
        }
    }
    Component { id: factory; Main { audioDevices: fakeAudioDevices; audioLevelTest: fakeAudioLevelTest } }
    property var app
    function init() {
        failOnWarning(/Binding loop detected/)
        fakeAudioDevices.systemDevices = [
            { id: "spk-a", name: "扬声器 A", isDefault: false, displayName: "扬声器 A" },
            { id: "spk-b", name: "扬声器 B", isDefault: true, displayName: "扬声器 B（默认）" }
        ]
        fakeAudioDevices.microphoneDevices = [
            { id: "mic-a", name: "麦克风 A", isDefault: true, displayName: "麦克风 A（默认）" },
            { id: "mic-b", name: "麦克风 B", isDefault: false, displayName: "麦克风 B" }
        ]
        fakeAudioDevices.refreshCount = 0
        fakeAudioLevelTest.reset()
        app = createTemporaryObject(factory, null)
        verify(app !== null)
    }
    function cleanup() { app.demoController.cancelChecks(); app.demoController.finishInterview(); app.destroy() }
    function test_aboutPageShowsIntroductionAndVersion() {
        const entry = findChild(app, "navAbout")
        verify(entry !== null)
        compare(entry.text, "关于我们")
        compare(entry.iconName, "info")
        mouseClick(entry)

        compare(app.demoController.route, "about")
        verify(entry.selected)
        const loader = findChild(app, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        compare(loader.item.objectName, "aboutPage")
        const header = findChild(loader.item, "aboutHeader")
        verify(header !== null)
        compare(header.title, "关于我们")
        verify(Qt.application.version.length > 0)
        compare(header.subtitle, "版本 " + Qt.application.version)
        const description = findChild(loader.item, "aboutDescription")
        verify(description !== null)
        compare(description.text,
                "春风是一款桌面端 AI 实时对话辅助工具，帮助你理解对方的问题、请求和异议，并参考回应建议组织表达。")

        mouseClick(findChild(app, "navHome"))
        compare(app.demoController.route, "home")
        verify(!entry.selected)
        tryCompare(loader, "status", Loader.Ready)
        verify(loader.item.objectName !== "aboutPage")
    }
    function test_routesLoad() {
        const controller = app.demoController
        for (const route of ["home", "background", "model", "audio", "history"]) {
            controller.route = route
            wait(50)
            const loader = findChild(app, "pageLoader")
            compare(loader.status, Loader.Ready)
            verify(loader.item !== null)
        }
        controller.openRecord("sample-1")
        wait(50)
        compare(findChild(app, "pageLoader").status, Loader.Ready)
    }
    function test_compactSettingsAlignment() {
        compare(app.width, 820)
        compare(app.height, 560)
        app.demoController.route = "audio"
        wait(80)
        const system = findChild(app, "systemDeviceField")
        const mic = findChild(app, "microphoneDeviceField")
        const engine = findChild(app, "engineActionButton")
        verify(system !== null && mic !== null && engine !== null)
        compare(system.mapToItem(app.contentItem, 0, 0).x, mic.mapToItem(app.contentItem, 0, 0).x)
        compare(system.width, mic.width)
        compare(system.height, 38)
        fuzzyCompare(system.mapToItem(app.contentItem, system.width, 0).x, engine.mapToItem(app.contentItem, engine.width, 0).x, 1)
        const fieldLeft = system.mapToItem(app.contentItem, 0, 0).x
        const fieldWidth = system.width
        app.demoController.route = "model"
        wait(80)
        for (const name of ["endpointField", "apiKeyField", "modelNameField"]) {
            const field = findChild(app, name)
            verify(field !== null)
            compare(field.mapToItem(app.contentItem, 0, 0).x, fieldLeft)
            compare(field.width, fieldWidth)
            compare(field.height, 38)
        }
        app.width = 760; app.height = 520
        for (const route of ["home", "background", "model", "audio", "history"]) {
            app.demoController.route = route
            wait(50)
            compare(app.width, 760)
            compare(app.height, 520)
            compare(findChild(app, "pageLoader").status, Loader.Ready)
        }
    }
    function test_audioDeviceListsAndEmptyState() {
        app.demoController.route = "audio"
        const system = findChild(app, "systemDeviceField")
        const mic = findChild(app, "microphoneDeviceField")
        verify(system !== null && mic !== null)
        compare(fakeAudioDevices.refreshCount, 1)
        compare(system.count, 2)
        compare(system.currentText, "扬声器 B（默认）")
        compare(system.textAt(0), "扬声器 A")
        compare(mic.count, 2)
        compare(mic.currentText, "麦克风 A（默认）")
        compare(mic.textAt(1), "麦克风 B")
        system.activated(1)
        system.currentIndex = 0
        system.activated(0)
        mic.currentIndex = 1
        mic.activated(1)
        compare(system.currentText, "扬声器 A")
        compare(mic.currentText, "麦克风 B")
        // A refresh can reorder devices or change the default; keep the
        // user's selection by ID and refresh the displayed default marker.
        fakeAudioDevices.systemDevices = [
            { id: "spk-b", name: "扬声器 B", isDefault: false, displayName: "扬声器 B" },
            { id: "spk-a", name: "扬声器 A", isDefault: true, displayName: "扬声器 A（默认）" }
        ]
        compare(system.currentText, "扬声器 A（默认）")
        compare(system.selectedDeviceId, "spk-a")
        fakeAudioDevices.microphoneDevices = [
            { id: "mic-b", name: "麦克风 B", isDefault: false, displayName: "麦克风 B" },
            { id: "mic-a", name: "麦克风 A", isDefault: true, displayName: "麦克风 A（默认）" }
        ]
        compare(mic.currentText, "麦克风 B")
        compare(mic.selectedDeviceId, "mic-b")
        system.popup.open()
        compare(fakeAudioDevices.refreshCount, 2)
        system.popup.close()
        mic.popup.open()
        compare(fakeAudioDevices.refreshCount, 3)
        mic.popup.close()
        fakeAudioDevices.systemDevices = []
        fakeAudioDevices.microphoneDevices = []
        tryCompare(system, "count", 0)
        tryCompare(mic, "count", 0)
        compare(system.currentIndex, -1)
        compare(mic.currentIndex, -1)
        compare(system.displayText, "暂无可用设备")
        compare(mic.displayText, "暂无可用设备")
        verify(!system.enabled && !mic.enabled)
        verify(findChild(app, "refreshAudioDevicesButton") === null)
        tryCompare(fakeAudioLevelTest, "microphoneId", "")
        tryCompare(fakeAudioLevelTest, "systemId", "")
    }
    function test_audioLevelTestControlsAndDeviceChanges() {
        app.demoController.route = "audio"
        const system = findChild(app, "systemDeviceField")
        const mic = findChild(app, "microphoneDeviceField")
        const button = findChild(app, "audioLevelTestButton")
        const systemMeter = findChild(app, "systemCaptureLevel")
        const micMeter = findChild(app, "microphoneCaptureLevel")
        const systemStatus = findChild(app, "systemCaptureStatus")
        const micStatus = findChild(app, "microphoneCaptureStatus")
        verify(system && mic && button && systemMeter && micMeter && systemStatus && micStatus)
        compare(fakeAudioLevelTest.microphoneId, "mic-a")
        compare(fakeAudioLevelTest.systemId, "spk-b")
        compare(button.text, "测试收音")
        compare(button.iconName, "play")
        compare(systemStatus.text, "等待测试")
        compare(micStatus.text, "等待测试")
        verify(button.enabled)
        button.clicked()
        compare(fakeAudioLevelTest.starts, 1)
        compare(fakeAudioLevelTest.microphoneId, "mic-a")
        compare(fakeAudioLevelTest.systemId, "spk-b")
        compare(button.text, "停止测试")
        compare(button.iconName, "stop")
        fakeAudioLevelTest.systemLevel = 0.72
        fakeAudioLevelTest.microphoneLevel = 0.31
        fakeAudioLevelTest.systemStatus = "正在接收"
        fakeAudioLevelTest.microphoneStatus = "设备异常"
        fuzzyCompare(systemMeter.value, 0.72, 0.001)
        fuzzyCompare(micMeter.value, 0.31, 0.001)
        compare(systemStatus.text, "正在接收")
        compare(micStatus.text, "设备异常")
        mic.currentIndex = 1
        mic.activated(1)
        system.currentIndex = 0
        system.activated(0)
        tryCompare(fakeAudioLevelTest, "microphoneId", "mic-b")
        tryCompare(fakeAudioLevelTest, "systemId", "spk-a")
        compare(fakeAudioLevelTest.stops, 1)
        compare(button.text, "正在停止…")
        verify(!button.enabled)
        compare(systemMeter.value, 0)
        compare(micMeter.value, 0)
        fakeAudioLevelTest.finishStop()
        compare(button.text, "测试收音")
        verify(button.enabled)
        button.clicked()
        compare(fakeAudioLevelTest.starts, 2)
        button.clicked()
        compare(fakeAudioLevelTest.stops, 2)
        compare(button.text, "正在停止…")
        verify(!button.enabled)
        fakeAudioLevelTest.finishStop()
        button.clicked()
        compare(fakeAudioLevelTest.starts, 3)
        fakeAudioDevices.microphoneDevices = []
        tryCompare(fakeAudioLevelTest, "microphoneId", "")
        compare(fakeAudioLevelTest.systemId, "spk-a")
        compare(fakeAudioLevelTest.stops, 3)
        compare(button.text, "正在停止…")
        verify(!button.enabled)
        fakeAudioLevelTest.finishStop()
        compare(button.text, "测试收音")
        compare(systemMeter.value, 0)
        compare(micMeter.value, 0)
        button.clicked()
        compare(fakeAudioLevelTest.starts, 4)
        // Leaving the page before a deferred selection update runs must still
        // stop the capture and must not send a late command to the bridge.
        system.currentIndex = 1
        system.activated(1)
        const updatesBeforeLeaving = fakeAudioLevelTest.deviceUpdates
        app.demoController.route = "home"
        tryCompare(fakeAudioLevelTest, "active", false)
        compare(fakeAudioLevelTest.stops, 4)
        wait(0)
        compare(fakeAudioLevelTest.deviceUpdates, updatesBeforeLeaving)
    }

    function test_audioMetersRemainAlignedWithLongStatus() {
        app.demoController.route = "audio"
        const system = findChild(app, "systemCaptureLevel")
        const mic = findChild(app, "microphoneCaptureLevel")
        const systemStatus = findChild(app, "systemCaptureStatus")
        const micStatus = findChild(app, "microphoneCaptureStatus")
        const panel = findChild(app, "audioLevelMeterPanel")
        const button = findChild(app, "audioLevelTestButton")
        verify(system && mic && systemStatus && micStatus && panel && button)
        fakeAudioLevelTest.systemLevel = 0.6
        fakeAudioLevelTest.microphoneLevel = 0.12
        if (testArtifactDirectory) {
            fakeAudioLevelTest.active = true
            fakeAudioLevelTest.systemStatus = "正在测试"
            fakeAudioLevelTest.microphoneStatus = "正在测试"
            for (const width of [820, 760]) {
                app.width = width
                wait(140)
                grabImage(app.contentItem).save(testArtifactDirectory + "/audio-meter-running-" + width + ".png")
            }
        }
        fakeAudioLevelTest.systemStatus = "系统音频权限未授权，请在系统设置中允许访问"
        fakeAudioLevelTest.microphoneStatus = "正在测试"
        for (const width of [820, 760]) {
            app.width = width
            wait(testArtifactDirectory ? 140 : 30)
            if (testArtifactDirectory)
                grabImage(app.contentItem).save(testArtifactDirectory + "/audio-meter-error-" + width + ".png")
            fuzzyCompare(system.mapToItem(app.contentItem, 0, 0).x,
                         mic.mapToItem(app.contentItem, 0, 0).x, 1)
            fuzzyCompare(system.mapToItem(app.contentItem, system.width, 0).x,
                         mic.mapToItem(app.contentItem, mic.width, 0).x, 1)
            fuzzyCompare(systemStatus.mapToItem(app.contentItem, 0, 0).x,
                         micStatus.mapToItem(app.contentItem, 0, 0).x, 1)
            const panelTopLeft = panel.mapToItem(app.contentItem, 0, 0)
            const buttonTopLeft = button.mapToItem(app.contentItem, 0, 0)
            verify(buttonTopLeft.x >= systemStatus.mapToItem(app.contentItem, systemStatus.width, 0).x)
            verify(buttonTopLeft.x + button.width <= panelTopLeft.x + panel.width)
            verify(buttonTopLeft.y < panelTopLeft.y + panel.height &&
                   buttonTopLeft.y + button.height > panelTopLeft.y)
            verify(buttonTopLeft.x + button.width <= app.width)
            verify(system.width >= 60)
            verify(systemStatus.height >= systemStatus.contentHeight)
            fuzzyCompare(system.value, 0.6, 0.001)
            fuzzyCompare(mic.value, 0.12, 0.001)
        }
    }
    function test_audioLevelTestCanCancelStartupRollback() {
        app.demoController.route = "audio"
        const button = findChild(app, "audioLevelTestButton")
        verify(button !== null && button.enabled)
        button.clicked()
        compare(fakeAudioLevelTest.starts, 1)
        fakeAudioLevelTest.stopping = true
        verify(fakeAudioLevelTest.active)
        compare(button.text, "停止测试")
        verify(button.enabled)
        button.clicked()
        compare(fakeAudioLevelTest.stops, 1)
        verify(!fakeAudioLevelTest.active && fakeAudioLevelTest.stopping)
        compare(button.text, "正在停止…")
        verify(!button.enabled)
        fakeAudioLevelTest.finishStop()
        compare(button.text, "测试收音")
        verify(button.enabled)
    }
    function test_preflightFitsMinimumWindow() {
        app.width = 760; app.height = 520
        const controller = app.demoController
        controller.checkInterval = 100000
        controller.startScenario = "engine"
        controller.startChecks()
        for (let step = 0; step < 5; ++step) controller.advanceCheck()
        const dialog = findChild(app, "preflightDialog")
        tryCompare(dialog, "opened", true)
        compare(controller.checkState, "failed")
        verify(dialog.y >= 0)
        verify(dialog.y + dialog.height <= app.contentItem.height)
    }
    function test_transcriptionTestDialogLifecycle() {
        const controller = app.demoController
        const test = controller.transcriptionTest
        const recordsBefore = JSON.stringify(controller.records)
        const backgroundBefore = controller.backgroundText
        controller.route = "audio"
        test.interval = 100000
        wait(80)
        mouseClick(findChild(app, "testTranscriptionButton"))
        const dialog = findChild(app, "transcriptionTestDialog")
        tryCompare(dialog, "opened", true)
        compare(test.status, "running")
        verify(dialog.x >= 0 && dialog.y >= 0)
        verify(dialog.x + dialog.width <= app.width)
        verify(dialog.y + dialog.height <= app.height)
        for (let i = 0; i < 70; ++i) test.advanceTick()
        verify(test.lines.some(line => line.source === "spk"))
        verify(test.lines.some(line => line.source === "mic"))
        verify(test.spkLevel > 0)
        wait(100)
        if (testArtifactDirectory) grabImage(app.contentItem).save(testArtifactDirectory + "/audio-test-running.png")
        mouseClick(findChild(app, "audioTestStopButton"))
        compare(test.status, "stopped")
        const stoppedText = JSON.stringify(test.lines)
        test.advanceTick()
        compare(JSON.stringify(test.lines), stoppedText)
        compare(test.spkLevel, 0)
        compare(test.micLevel, 0)
        wait(100)
        if (testArtifactDirectory) grabImage(app.contentItem).save(testArtifactDirectory + "/audio-test-stopped.png")
        mouseClick(findChild(app, "audioTestRestartButton"))
        compare(test.status, "running")
        compare(test.lines.length, 0)
        test.spkAvailable = false
        for (let i = 0; i < 64; ++i) test.advanceTick()
        compare(test.spkState, "error")
        verify(test.lines.every(line => line.source !== "spk"))
        verify(test.lines.some(line => line.source === "mic"))
        wait(100)
        if (testArtifactDirectory) grabImage(app.contentItem).save(testArtifactDirectory + "/audio-test-channel-error.png")
        mouseClick(findChild(app, "closeTranscriptionTestButton"))
        tryCompare(dialog, "opened", false)
        compare(test.lines.length, 0)
        test.spkAvailable = true
        app.width = 760; app.height = 520
        mouseClick(findChild(app, "testTranscriptionButton"))
        tryCompare(dialog, "opened", true)
        for (let i = 0; i < 70; ++i) test.advanceTick()
        verify(dialog.x >= 0 && dialog.y >= 0)
        verify(dialog.y + dialog.height <= app.height)
        wait(100)
        if (testArtifactDirectory) grabImage(app.contentItem).save(testArtifactDirectory + "/audio-test-minimum.png")
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "opened", false)
        verify(!test.opened)
        verify(!controller.interviewActive)
        compare(controller.messages.length, 0)
        compare(controller.currentQuestions.length, 0)
        compare(JSON.stringify(controller.records), recordsBefore)
        compare(controller.backgroundText, backgroundBefore)
    }
    function test_onlyHoveredMenuExpands() {
        app.requestActivate()
        const home = findChild(app, "navHome")
        const audio = findChild(app, "navAudio")
        home.forceActiveFocus(Qt.TabFocusReason)
        tryCompare(home, "visualFocus", true)
        mouseMove(audio, 20, 20)
        tryCompare(audio, "hovered", true)
        verify(audio.expanded)
        verify(!home.expanded)
        compare(findChild(app, "sidebar").width, 64)
    }
    function test_renderedPreviews() {
        if (!testArtifactDirectory) return
        const controller = app.demoController
        mouseMove(app.contentItem, app.width - 40, 24)
        wait(240)
        for (const route of ["home", "background", "model", "audio", "history", "about"]) {
            controller.route = route
            wait(240)
            grabImage(app.contentItem).save(testArtifactDirectory + "/" + route + ".png")
        }
        controller.route = "home"
        mouseMove(findChild(app, "navHistory"), 20, 20)
        wait(240)
        grabImage(app.contentItem).save(testArtifactDirectory + "/menu-hover.png")
        mouseMove(app.contentItem, app.width - 40, 24)
        controller.openRecord("sample-1")
        wait(100)
        grabImage(app.contentItem).save(testArtifactDirectory + "/record.png")
        app.width = 760; app.height = 520
        for (const route of ["home", "background", "model", "audio", "history", "record", "about"]) {
            controller.route = route
            wait(140)
            grabImage(app.contentItem).save(testArtifactDirectory + "/" + route + "-minimum.png")
        }
        controller.route = "home"
        controller.checkInterval = 100000
        controller.startScenario = "engine"
        controller.startChecks()
        for (let step = 0; step < 5; ++step) controller.advanceCheck()
        wait(240)
        grabImage(app.contentItem).save(testArtifactDirectory + "/preflight-minimum.png")
        controller.cancelChecks()
        app.width = 820; app.height = 560
        controller.streamInterval = 1
        controller.beginInterview()
        tryCompare(controller, "generating", false, 5000)
        grabImage(findChild(app, "answerWindow").contentItem).save(testArtifactDirectory + "/suggestion-question.png")
        for (let i = 0; i < 44; ++i) controller.advanceSession()
        tryCompare(controller, "generating", false, 5000)
        grabImage(findChild(app, "answerWindow").contentItem).save(testArtifactDirectory + "/suggestion-request.png")
        controller.queueQuestion(3)
        tryCompare(controller, "generating", false, 5000)
        grabImage(findChild(app, "answerWindow").contentItem).save(testArtifactDirectory + "/suggestion-objection.png")
        controller.togglePause()
        wait(100)
        grabImage(findChild(app, "transcriptWindow").contentItem).save(testArtifactDirectory + "/transcript.png")
        grabImage(findChild(app, "answerWindow").contentItem).save(testArtifactDirectory + "/answer.png")
    }
    function test_enterCloseEitherWindowAndReenter() {
        const controller = app.demoController
        controller.beginInterview()
        tryCompare(app, "visible", false)
        tryCompare(app, "transcriptVisible", true)
        tryCompare(app, "answerVisible", true)
        findChild(app, "answerWindow").close()
        tryCompare(app, "visible", true)
        compare(controller.route, "record")
        compare(app.transcriptVisible, false)
        compare(app.answerVisible, false)
        controller.beginInterview()
        tryCompare(app, "visible", false)
        findChild(app, "transcriptWindow").close()
        tryCompare(app, "visible", true)
        compare(app.answerVisible, false)
    }
    function test_collapseKeepsBothWindowsReachable() {
        const controller = app.demoController
        controller.beginInterview()
        controller.transcriptCollapsed = true
        controller.answerCollapsed = true
        wait(30)
        verify(app.transcriptVisible && app.answerVisible)
        compare(findChild(app, "transcriptWindow").height, 64)
        compare(findChild(app, "answerWindow").height, 54)
        controller.transcriptCollapsed = false
        controller.answerCollapsed = false
        wait(30)
        verify(findChild(app, "transcriptWindow").height > 64)
        verify(findChild(app, "answerWindow").height > 54)
    }
}
