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
    QtObject {
        id: fakeOffline
        property string modelPath: ""
        property string engineStatus: "stopped"
        property string engineError: ""
        property int loadCount: 0
        property string attemptedModelPath: ""
        property bool opened: false
        property string status: "idle"
        property string elapsedText: "00:00"
        property string spkDevice: ""
        property string micDevice: ""
        property string spkState: "waiting"
        property string micState: "waiting"
        property real spkLevel: 0
        property real micLevel: 0
        property var lines: []
        property string errorMessage: ""
        property string selectedMic: ""
        property string selectedSpk: ""
        property bool conversationActive: false
        property var conversationLines: lines
        property int conversationStarts: 0
        property int conversationStops: 0
        property int conversationAborts: 0
        signal changed()
        signal conversationFinished(var lines)
        function loadEngine(model) {
            loadCount++
            attemptedModelPath = model
            engineError = ""
            engineStatus = "starting"
        }
        function unloadEngine() { engineStatus = "stopping" }
        function setDevices(mic, spk) { selectedMic = mic; selectedSpk = spk }
        function pathFromUrl(url) { return String(url) }
        function open() { opened = true }
        function close() { opened = false }
        function stop() {}
        function restart() {}
        function startConversation() {
            if (engineStatus !== "ready" || conversationActive)
                return false
            conversationStarts++
            if (status === "error")
                changed() // Discovering default devices can emit before the old status is replaced.
            lines = []
            errorMessage = ""
            conversationActive = true
            status = "starting"
            return true
        }
        function setConversationPaused(paused) {
            if (!conversationActive)
                return false
            status = paused ? "paused" : "running"
            return true
        }
        function stopConversation() {
            conversationStops++
            status = "stopping"
        }
        function finishConversation() {
            conversationActive = false
            status = "stopped"
            conversationFinished(lines)
        }
        function abortConversation() {
            conversationAborts++
            finishConversation()
        }
    }
    Component { id: realFactory; Main { audioDevices: fakeAudioDevices; audioLevelTest: fakeAudioLevelTest; offlineTranscription: fakeOffline } }
    Component {
        id: conversationStoreFactory
        QtObject {
            property bool ready: true
            property bool failSave: false
            property string errorMessage: ""
            property var records: []
            property var details: ({})
            property int sequence: 0

            function newRecordId() { return "native-record-" + ++sequence }
            function record(id) { return details[id] ?? ({}) }
            function saveRecord(value) {
                if (failSave) {
                    errorMessage = "无法写入对话记录。"
                    return false
                }
                details[value.id] = value
                records = [{ id: value.id, title: value.title, date: value.date, duration: value.duration,
                             model: value.model, isDemo: false }].concat(records)
                return true
            }
            function renameRecord(id, title) {
                details[id].title = title
                records = records.map(item => item.id === id ? Object.assign({}, item, { title: title }) : item)
                return true
            }
            function deleteRecord(id) {
                delete details[id]
                records = records.filter(item => item.id !== id)
                return true
            }
        }
    }
    Component {
        id: conversationAdviceFactory
        QtObject {
            property string question: ""
            property string triggerKind: ""
            property string answer: ""
            property bool generating: false
            property string statusMessage: "请配置 DeepSeek API Key 后使用回应建议。"
            property string capturedBackground: ""
            property bool listening: false
            property int beginCount: 0
            property int finishCount: 0
            property var observations: []
            property var archivedAdvice: []

            function beginSession(background) {
                beginCount++
                capturedBackground = background
            }
            function considerTranscript(lines) {
                observations = observations.concat([{ listening: listening, lines: Array.from(lines) }])
            }
            function setListening(value) { listening = value }
            function finishSession() {
                finishCount++
                return archivedAdvice
            }
        }
    }
    Component {
        id: modelConnectionFactory
        QtObject {
            readonly property string apiEndpoint: "https://api.deepseek.com"
            readonly property var availableModels: ["deepseek-flash", "deepseek-v4-pro"]
            property string savedModelName: "deepseek-flash"
            property string savedApiKey: ""
            property string draftModelName: savedModelName
            property string draftApiKey: ""
            readonly property bool modelConfigured: savedApiKey.length > 0
            readonly property bool configurationDirty: draftModelName !== savedModelName || draftApiKey.trim() !== savedApiKey
            property string verificationStatus: "unconfigured"
            property string verificationMessage: ""
            property int verificationCount: 0
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
    Component {
        id: modelTestChatFactory
        QtObject {
            property string modelName: ""
            property bool running: false
            property int beginCount: 0
            property ListModel turns: ListModel {}

            function beginSession() {
                if (modelName.length > 0)
                    return false
                beginCount++
                modelName = "deepseek-v4-pro"
                return true
            }
            function sendPrompt(prompt) {
                if (modelName.length === 0 || running || !prompt.trim())
                    return false
                turns.append({ promptText: prompt, replyText: "", turnState: "streaming", turnMessage: "" })
                running = true
                return true
            }
            function updateReply(reply) { turns.setProperty(turns.count - 1, "replyText", reply) }
            function finishTurn(state, message) {
                turns.set(turns.count - 1, { turnState: state, turnMessage: message })
                running = false
            }
            function retryLast() {
                if (running || turns.count === 0 || turns.get(turns.count - 1).turnState !== "failed")
                    return false
                turns.set(turns.count - 1, { turnState: "streaming", turnMessage: "" })
                running = true
                return true
            }
            function endSession() {
                turns.clear()
                modelName = ""
                running = false
            }
        }
    }
    Component {
        id: backgroundDocumentFactory
        QtObject {
            property string backgroundText: ""
            property string errorMessage: ""
            property url importedFile
            property url failingFile

            function importMarkdown(file) {
                importedFile = file
                if (String(file) === String(failingFile)) {
                    errorMessage = "Markdown 文件内容无效。"
                    return false
                }
                errorMessage = ""
                backgroundText = "# 项目背景\n\n**重点**\n\n<p>保留 Markdown 源码。</p>"
                return true
            }
        }
    }
    Component {
        id: backgroundAnalysisFactory
        QtObject {
            property bool configured: false
            property bool running: false
            property string resultText: ""
            property string statusMessage: ""
            property bool failed: false

            function startAnalysis() {
                resultText = ""
                statusMessage = ""
                failed = false
                running = true
                return true
            }
            function cancelAnalysis() {
                running = false
                statusMessage = "分析已停止，以下为已有内容。"
            }
        }
    }
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
    function test_nativeConversationWaitsForCaptureAndSavesDrainedTranscript() {
        const store = createTemporaryObject(conversationStoreFactory, testCase)
        fakeOffline.engineStatus = "stopped"
        fakeOffline.engineError = ""
        fakeOffline.opened = false
        fakeOffline.conversationActive = false
        fakeOffline.conversationStarts = 0
        fakeOffline.conversationStops = 0
        const background = createTemporaryObject(backgroundDocumentFactory, testCase, { backgroundText: "# 当前背景" })
        const nativeApp = createTemporaryObject(realFactory, null, {
            conversationStore: store, backgroundDocument: background
        })
        verify(nativeApp !== null)
        const controller = nativeApp.demoController
        verify(!controller.isDemo)
        compare(controller.records.length, 0)
        mouseClick(findChild(nativeApp, "startInterviewButton"))
        const preflight = findChild(nativeApp, "preflightDialog")
        tryCompare(preflight, "opened", true)
        compare(controller.checkState, "failed")
        compare(fakeOffline.conversationStarts, 0)
        const engineButton = findChild(preflight, "loadConversationEngineButton")
        tryVerify(() => {
            const bottomRight = engineButton.mapToItem(nativeApp.contentItem, engineButton.width, engineButton.height)
            return bottomRight.x <= nativeApp.width && bottomRight.y <= nativeApp.height
        })
        mouseClick(engineButton)
        tryCompare(controller, "route", "offline")
        tryCompare(preflight, "opened", false)

        fakeOffline.engineStatus = "ready"
        fakeOffline.status = "error"
        fakeOffline.modelPath = "/models/test-asr.gguf"
        controller.route = "home"
        const loader = findChild(nativeApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        fakeAudioLevelTest.start("mic-a", "spk-b")
        const startButton = findChild(loader.item, "startInterviewButton")
        verify(startButton !== null)
        mouseClick(startButton)
        compare(fakeAudioLevelTest.stops, 1)
        compare(fakeOffline.conversationStarts, 0)
        fakeAudioLevelTest.finishStop()
        tryCompare(fakeOffline, "conversationStarts", 1)
        verify(nativeApp.visible)
        fakeOffline.status = "running"
        tryCompare(nativeApp, "transcriptVisible", true)
        verify(!nativeApp.answerVisible)
        compare(controller.liveConversationMessages.length, 0)
        background.backgroundText = "# 之后更新的背景"
        fakeOffline.lines = [
            { id: 1, source: "spk", time: "00:01", text: "对方说话", final: true, incomplete: false },
            { id: 2, source: "mic", time: "00:02", text: "我的语音", final: false, incomplete: false }
        ]
        compare(controller.liveConversationMessages[0].role, "other")
        compare(controller.liveConversationMessages[1].role, "me")
        compare(controller.liveConversationMessages[1].source, "mic")
        controller.togglePause()
        verify(controller.paused)
        controller.togglePause()
        verify(!controller.paused)

        findChild(nativeApp, "transcriptWindow").close()
        compare(fakeOffline.conversationStops, 1)
        compare(store.records.length, 0)
        verify(nativeApp.transcriptVisible && !nativeApp.visible)
        controller.startChecks()
        compare(fakeOffline.conversationStarts, 1)
        fakeOffline.lines = [fakeOffline.lines[0], {
            id: 2, source: "mic", time: "00:02", text: "我的语音尾部", final: true, incomplete: false
        }]
        fakeOffline.finishConversation()
        tryCompare(nativeApp, "visible", true)
        compare(controller.route, "record")
        compare(store.records.length, 1)
        compare(controller.selectedRecord.backgroundText, "# 当前背景")
        compare(controller.selectedRecord.transcript[1].text, "我的语音尾部")
        compare(controller.selectedRecord.questions.length, 0)
        compare(controller.selectedRecord.model, "test-asr.gguf")
        verify(controller.selectedRecord.startedAt > 0)
        const id = controller.selectedRecord.id
        controller.route = "history"
        controller.openRecord(id)
        compare(controller.selectedRecord.transcript.length, 2)
        controller.renameRecord(id, "改名后的真实对话")
        compare(store.record(id).title, "改名后的真实对话")
        controller.deleteRecord(id)
        compare(store.records.length, 0)
    }

    function test_nativeConversationQuitKeepsUnsavedRecordReadable() {
        const store = createTemporaryObject(conversationStoreFactory, testCase, { failSave: true })
        fakeOffline.engineStatus = "ready"
        fakeOffline.opened = false
        fakeOffline.conversationActive = false
        fakeOffline.conversationStarts = 0
        fakeOffline.conversationStops = 0
        fakeOffline.conversationAborts = 0
        const nativeApp = createTemporaryObject(realFactory, null, { conversationStore: store })
        const controller = nativeApp.demoController
        controller.startChecks()
        tryCompare(fakeOffline, "conversationStarts", 1)
        fakeOffline.status = "running"
        tryCompare(nativeApp, "transcriptVisible", true)
        fakeOffline.lines = [{ id: 1, source: "mic", time: "00:01", text: "退出前的内容", final: false, incomplete: true }]
        nativeApp.applicationQuitting = true
        compare(fakeOffline.conversationStops, 1)
        compare(fakeOffline.conversationAborts, 0)
        verify(nativeApp.conversationQuitPending)
        fakeOffline.lines = [{ id: 1, source: "mic", time: "00:01", text: "退出前的完整内容", final: true, incomplete: false }]
        fakeOffline.finishConversation()
        verify(!nativeApp.conversationQuitPending)
        verify(!nativeApp.visible && !nativeApp.transcriptVisible)
        compare(store.records.length, 0)
        verify(controller.selectedRecord.unsaved)
        compare(controller.selectedRecord.transcript[0].text, "退出前的完整内容")
        compare(findChild(nativeApp, "notificationToast").message, "无法写入对话记录。")
        verify(nativeApp.conversationBlocksQuit)
        nativeApp.showUnsavedConversationQuitDialog()
        const dialog = findChild(nativeApp, "unsavedConversationQuitDialog")
        tryCompare(dialog, "opened", true)
        verify(nativeApp.visible)
        dialog.close()
        const id = controller.selectedRecord.id
        verify(!controller.retryRecordSave(id))
        verify(controller.selectedRecord.unsaved && nativeApp.conversationBlocksQuit)
        store.failSave = false
        verify(controller.retryRecordSave(id))
        compare(store.record(id).transcript[0].text, "退出前的完整内容")
        verify(!nativeApp.conversationBlocksQuit)
    }
    function test_nativeConversationStreamsAndArchivesAdviceOnQuit() {
        const store = createTemporaryObject(conversationStoreFactory, testCase)
        const advice = createTemporaryObject(conversationAdviceFactory, testCase)
        const background = createTemporaryObject(backgroundDocumentFactory, testCase, { backgroundText: "# 会话背景" })
        fakeOffline.engineStatus = "ready"
        fakeOffline.opened = false
        fakeOffline.conversationActive = false
        fakeOffline.conversationStarts = 0
        fakeOffline.conversationStops = 0
        const nativeApp = createTemporaryObject(realFactory, null, {
            conversationStore: store,
            conversationAdvice: advice,
            backgroundDocument: background,
            width: 760,
            height: 520
        })
        verify(nativeApp !== null)
        const controller = nativeApp.demoController
        controller.startChecks()
        tryCompare(fakeOffline, "conversationStarts", 1)
        fakeOffline.status = "running"
        tryCompare(nativeApp, "answerVisible", true)
        verify(nativeApp.transcriptVisible)
        compare(advice.beginCount, 1)
        compare(advice.capturedBackground, "# 会话背景")
        verify(advice.listening)
        compare(advice.observations.length, 1)
        const panel = findChild(nativeApp, "conversationAnswerPanel")
        const answerWindow = findChild(nativeApp, "answerWindow")
        const transcriptWindow = findChild(nativeApp, "transcriptWindow")
        verify(panel !== null)
        verify(transcriptWindow.x + transcriptWindow.width < answerWindow.x)
        compare(findChild(panel, "answerEmptyState").text, "请配置 DeepSeek API Key 后使用回应建议。")

        background.backgroundText = "# 下一次背景"
        fakeOffline.lines = [
            { id: 1, source: "mic", time: "00:01", text: "我的补充说明", final: true, incomplete: false },
            { id: 2, source: "spk", time: "00:02", text: "<b>什么时候交付？</b>", final: true, incomplete: false }
        ]
        compare(advice.observations[advice.observations.length - 1].lines[0].source, "mic")
        compare(advice.observations[advice.observations.length - 1].lines[1].source, "spk")
        advice.statusMessage = ""
        advice.question = "<b>什么时候交付？</b>"
        advice.triggerKind = "question"
        advice.generating = true
        advice.answer = "先确认"
        compare(panel.question, advice.question)
        compare(findChild(panel, "adviceQuestionText").textFormat, Text.PlainText)
        compare(panel.triggerKind, "question")
        compare(panel.answer, "先确认")
        verify(panel.generating)
        advice.answer += "验收标准，再给出时间。"
        compare(panel.answer, "先确认验收标准，再给出时间。")

        controller.togglePause()
        verify(!advice.listening)
        fakeOffline.lines = fakeOffline.lines.concat([
            { id: 3, source: "spk", time: "00:03", text: "暂停前的尾部", final: true, incomplete: false }
        ])
        verify(!advice.observations[advice.observations.length - 1].listening)
        controller.togglePause()
        verify(advice.listening)

        advice.archivedAdvice = [{ kind: "question", question: advice.question, answer: advice.answer,
                                   time: "00:02", incomplete: true }]
        nativeApp.applicationQuitting = true
        compare(fakeOffline.conversationStops, 1)
        verify(!advice.listening)
        compare(advice.finishCount, 0)
        fakeOffline.finishConversation()
        nativeApp.applicationQuitting = false
        compare(advice.finishCount, 1)
        compare(controller.selectedRecord.questions[0].answer, "先确认验收标准，再给出时间。")
        compare(controller.selectedRecord.questions[0].incomplete, true)
        compare(controller.selectedRecord.backgroundText, "# 会话背景")
        verify(!nativeApp.answerVisible)
    }
    function test_offlineModelLoadsDraftAndRequiresUnloadBeforeChanging() {
        fakeOffline.modelPath = "/models/remembered.gguf"
        fakeOffline.engineStatus = "stopped"
        fakeOffline.engineError = ""
        fakeOffline.loadCount = 0
        fakeOffline.attemptedModelPath = ""
        const realApp = createTemporaryObject(realFactory, null)
        verify(realApp !== null)
        realApp.demoController.route = "offline"
        const model = findChild(realApp, "offlineModelField")
        verify(model !== null)
        compare(findChild(realApp, "offlineRuntimeField"), null)
        const loadButton = findChild(realApp, "offlineLoadButton")
        compare(model.text, "/models/remembered.gguf")
        compare(fakeOffline.loadCount, 0) // Restoring the path must not load the model.
        verify(loadButton.enabled)
        if (testArtifactDirectory) {
            wait(100)
            grabImage(realApp.contentItem).save(testArtifactDirectory + "/offline-model-waiting.png")
        }
        model.text = "  "
        verify(!loadButton.enabled)
        model.text = "  /models/asr.gguf  "
        verify(loadButton.enabled)
        compare(findChild(realApp, "offlineSaveButton"), null)
        const picker = findChild(realApp, "offlineModelPicker")
        const result = findChild(realApp, "offlineModelResult")
        verify(picker !== null && result !== null)
        verify(!result.visible)
        const settingsPage = findChild(realApp, "pageLoader").item
        wait(50)
        settingsPage.contentY = Math.max(0, settingsPage.contentHeight - settingsPage.height)
        wait(50)
        mouseClick(loadButton)
        compare(fakeOffline.loadCount, 1)
        compare(fakeOffline.attemptedModelPath, "/models/asr.gguf")
        compare(fakeOffline.modelPath, "/models/remembered.gguf")
        compare(loadButton.text, "正在加载…")
        verify(!loadButton.enabled)
        verify(model.readOnly)
        verify(!picker.enabled)
        if (testArtifactDirectory) {
            wait(100)
            grabImage(realApp.contentItem).save(testArtifactDirectory + "/offline-model-loading.png")
        }
        fakeOffline.modelPath = "/models/asr.gguf"
        fakeOffline.engineStatus = "ready"
        compare(loadButton.text, "卸载模型")
        verify(loadButton.enabled)
        verify(model.readOnly)
        verify(!picker.enabled)
        verify(result.visible)
        compare(result.text, "已加载")
        if (testArtifactDirectory) {
            wait(100)
            grabImage(realApp.contentItem).save(testArtifactDirectory + "/offline-model-loaded.png")
        }
        fakeOffline.engineError = "模型已加载，但无法记住模型路径"
        compare(result.text, "模型已加载，但无法记住模型路径")
        compare(loadButton.text, "卸载模型")
        mouseClick(loadButton)
        compare(loadButton.text, "正在卸载…")
        verify(!loadButton.enabled)
        verify(model.readOnly)
        fakeOffline.engineStatus = "stopped"
        fakeOffline.engineError = ""
        verify(!model.readOnly)
        verify(picker.enabled)
        verify(!result.visible)
        compare(fakeOffline.modelPath, "/models/asr.gguf")
        model.text = "/models/other.gguf"
        verify(loadButton.enabled)
        mouseClick(loadButton)
        compare(fakeOffline.attemptedModelPath, "/models/other.gguf")
        compare(fakeOffline.loadCount, 2)
        realApp.destroy()
    }
    function test_offlineFailedModelKeepsDraftAndAllowsRetry() {
        fakeOffline.modelPath = "/models/remembered.gguf"
        fakeOffline.engineStatus = "stopped"
        fakeOffline.engineError = ""
        fakeOffline.loadCount = 0
        fakeOffline.attemptedModelPath = ""
        const realApp = createTemporaryObject(realFactory, null)
        verify(realApp !== null)
        realApp.demoController.route = "offline"
        let model = findChild(realApp, "offlineModelField")
        let loadButton = findChild(realApp, "offlineLoadButton")
        model.text = "/models/invalid.gguf"
        const pageLoader = findChild(realApp, "pageLoader")
        const settingsPage = pageLoader.item
        wait(50)
        settingsPage.contentY = Math.max(0, settingsPage.contentHeight - settingsPage.height)
        wait(50)
        mouseClick(loadButton)
        compare(fakeOffline.loadCount, 1)
        realApp.demoController.route = "home"
        tryVerify(function() { return pageLoader.item !== null && pageLoader.item.objectName !== "offlineTranscriptionPage" })
        realApp.demoController.route = "offline"
        tryCompare(pageLoader.item, "objectName", "offlineTranscriptionPage")
        model = findChild(pageLoader.item, "offlineModelField")
        loadButton = findChild(pageLoader.item, "offlineLoadButton")
        compare(model.text, "/models/invalid.gguf")
        compare(loadButton.text, "正在加载…")
        verify(model.readOnly)
        fakeOffline.engineError = "无法加载模型"
        fakeOffline.engineStatus = "error"
        fakeOffline.modelPathChanged() // The real controller shares one notify signal for all properties.
        compare(model.text, "/models/invalid.gguf")
        realApp.demoController.route = "home"
        tryVerify(function() { return pageLoader.item !== null && pageLoader.item.objectName !== "offlineTranscriptionPage" })
        realApp.demoController.route = "offline"
        tryCompare(pageLoader.item, "objectName", "offlineTranscriptionPage")
        model = findChild(pageLoader.item, "offlineModelField")
        loadButton = findChild(pageLoader.item, "offlineLoadButton")
        compare(model.text, "/models/invalid.gguf")
        compare(fakeOffline.modelPath, "/models/remembered.gguf")
        verify(!model.readOnly)
        verify(findChild(pageLoader.item, "offlineModelPicker").enabled)
        const result = findChild(pageLoader.item, "offlineModelResult")
        verify(result.visible)
        compare(result.text, "无法加载模型")
        verify(loadButton.enabled)
        if (testArtifactDirectory) {
            wait(100)
            grabImage(realApp.contentItem).save(testArtifactDirectory + "/offline-model-error.png")
        }
        mouseClick(loadButton)
        compare(fakeOffline.loadCount, 2)
        compare(fakeOffline.attemptedModelPath, "/models/invalid.gguf")
        realApp.destroy()
    }
    function test_realTranscriptionWaitsForAudioCaptureToStop() {
        fakeOffline.engineStatus = "ready"
        fakeOffline.opened = false
        fakeAudioLevelTest.reset()
        const realApp = createTemporaryObject(realFactory, null)
        verify(realApp !== null)
        realApp.demoController.route = "audio"
        const button = findChild(realApp, "testTranscriptionButton")
        verify(button !== null)
        tryCompare(fakeOffline, "selectedMic", "mic-a")
        compare(fakeOffline.selectedSpk, "spk-b")
        fakeAudioLevelTest.active = true
        mouseClick(button)
        verify(!fakeOffline.opened)
        verify(fakeAudioLevelTest.stopping)
        fakeAudioLevelTest.finishStop()
        tryCompare(fakeOffline, "opened", true)
        fakeOffline.close()
        realApp.destroy()
    }
    function test_realTranscriptionCapsuleUpdatesInPlace() {
        fakeOffline.engineStatus = "ready"
        fakeOffline.opened = false
        fakeOffline.status = "running"
        fakeOffline.lines = []
        const realApp = createTemporaryObject(realFactory, null)
        verify(realApp !== null)
        realApp.demoController.route = "audio"
        fakeOffline.open()
        fakeOffline.lines = [{id: "one", source: "mic", time: "00:01", text: "你好", final: false}]
        const dialog = findChild(realApp, "transcriptionTestDialog")
        const list = findChild(dialog, "audioTestTranscript")
        tryCompare(list, "count", 1)
        tryVerify(function() { return list.itemAtIndex(0) !== null })
        const row = list.itemAtIndex(0)
        const capsule = findChild(row, "transcriptCapsule")
        verify(capsule !== null)
        compare(capsule.isComplete, false)
        compare(capsule.isIncomplete, false)
        fakeOffline.lines = [{id: "one", source: "mic", time: "00:01", text: "你好世界", final: true}]
        tryCompare(capsule, "isComplete", true)
        compare(list.itemAtIndex(0), row)
        fakeOffline.lines = [{id: "one", source: "mic", time: "00:01", text: "你好世界", final: false, incomplete: true}]
        tryCompare(capsule, "isIncomplete", true)
        verify(findChild(capsule, "transcriptCapsuleText").text.indexOf("未完成") >= 0)
        fakeOffline.close()
        realApp.destroy()
    }
    function test_realTranscriptionAlignsSourcesInTimeOrder() {
        fakeOffline.engineStatus = "ready"
        fakeOffline.opened = false
        fakeOffline.status = "running"
        fakeOffline.lines = []
        const realApp = createTemporaryObject(realFactory, null, {width: 760, height: 520})
        verify(realApp !== null)
        realApp.demoController.route = "audio"
        fakeOffline.open()
        fakeOffline.lines = [
            {id: "first", source: "mic", time: "00:01", final: true,
             text: "这段麦克风文字用来检查长句在统一的正文栏内换行后，是否保留时间和清楚可见的来源标签。"},
            {id: "second", source: "spk", time: "00:02", final: true,
             text: "这段系统声音文字用来检查长句在统一的正文栏内换行后，是否保留时间和清楚可见的来源标签。"},
            {id: "third", source: "mic", time: "00:03", final: true, text: "下一句。"}
        ]
        const dialog = findChild(realApp, "transcriptionTestDialog")
        const list = findChild(dialog, "audioTestTranscript")
        tryCompare(list, "count", 3)
        const micChannel = findChild(dialog, "micTestChannel")
        const spkChannel = findChild(dialog, "spkTestChannel")
        verify(micChannel !== null && spkChannel !== null)
        const micChannelLabel = findChild(micChannel, "audioTestChannelLabel")
        const spkChannelLabel = findChild(spkChannel, "audioTestChannelLabel")
        verify(micChannelLabel !== null && spkChannelLabel !== null)
        compare(micChannelLabel.text, "MIC")
        compare(spkChannelLabel.text, "SPK")
        list.following = false
        list.positionViewAtBeginning()
        tryVerify(function() { return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null })
        compare(list.model.get(0).lineTime, "00:01")
        compare(list.model.get(1).lineTime, "00:02")
        compare(list.model.get(2).lineTime, "00:03")

        const micRow = list.itemAtIndex(0)
        const spkRow = list.itemAtIndex(1)
        const mic = findChild(micRow, "transcriptCapsule")
        const spk = findChild(spkRow, "transcriptCapsule")
        compare(micRow.source, "MIC")
        compare(spkRow.source, "SPK")
        verify(mic !== null && spk !== null)
        compare(mic.x, spk.x)
        compare(mic.color, spk.color)
        const micMeta = findChild(micRow, "transcriptSourceMeta")
        const spkMeta = findChild(spkRow, "transcriptSourceMeta")
        verify(micMeta !== null && spkMeta !== null)
        compare(micMeta.x, spkMeta.x)
        compare(micMeta.width, spkMeta.width)
        verify(mic.x >= micMeta.x + micMeta.width)
        compare(findChild(micMeta, "transcriptSourceIcon").name, "mic")
        compare(findChild(spkMeta, "transcriptSourceIcon").name, "headphones")
        compare(findChild(micMeta, "transcriptSourceLabel").text, "MIC")
        compare(findChild(spkMeta, "transcriptSourceLabel").text, "SPK")
        verify(mic.isComplete && spk.isComplete)
        verify(findChild(mic, "transcriptCapsuleText").lineCount > 1)
        verify(findChild(spk, "transcriptCapsuleText").lineCount > 1)
        const history = fakeOffline.lines.slice()
        for (let i = 0; i < 12; ++i)
            history.push({id: "extra-" + i, source: i % 2 ? "mic" : "spk", time: "00:04",
                          final: true, text: "历史转写。"})
        fakeOffline.lines = history
        const latest = findChild(dialog, "audioTestLatestButton")
        tryCompare(latest, "visible", true)
        mouseClick(latest)
        tryCompare(list, "atYEnd", true)
        compare(list.following, true)
        if (testArtifactDirectory) {
            fakeOffline.spkDevice = "MacBook Air 扬声器"
            fakeOffline.micDevice = "MacBook Air 麦克风"
            fakeOffline.spkLevel = 0.42
            fakeOffline.micLevel = 0.24
            fakeOffline.elapsedText = "00:16"
            fakeOffline.lines = [
                {id: "first", source: "mic", time: "00:01", final: true, text: "我们先确认一下今天讨论的内容。"},
                {id: "second", source: "spk", time: "00:05", final: false, incomplete: true, text: "接下来核对时间和安排。"},
                {id: "third", source: "mic", time: "00:12", final: false, text: "好的，我这边可以清楚地听到。"}
            ]
            tryCompare(list.model.get(1), "incomplete", true)
            tryCompare(list, "count", 3)
            wait(120) // Let the animated level bars settle for the screenshot.
            waitForRendering(realApp.contentItem)
            grabImage(realApp.contentItem).save(testArtifactDirectory + "/transcription-timeline-minimum.png")
            fakeOffline.spkDevice = ""
            fakeOffline.micDevice = ""
            fakeOffline.spkLevel = 0
            fakeOffline.micLevel = 0
            fakeOffline.elapsedText = "00:00"
        }
        fakeOffline.close()
        realApp.destroy()
    }
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
    function test_backgroundPageImportsAndPreviewsMarkdown() {
        const validFile = Qt.resolvedUrl("../../../README.md")
        const invalidFile = Qt.resolvedUrl("../../core/README.md")
        const document = createTemporaryObject(backgroundDocumentFactory, testCase, { failingFile: invalidFile })
        const backgroundApp = createTemporaryObject(factory, null, { backgroundDocument: document })
        verify(backgroundApp !== null)
        backgroundApp.width = 760
        backgroundApp.height = 520
        backgroundApp.demoController.route = "background"
        const loader = findChild(backgroundApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const page = loader.item
        compare(findChild(page, "saveBackgroundButton"), null)
        const picker = findChild(page, "backgroundFileDialog")
        const importButton = findChild(page, "backgroundImportButton")
        const analysis = findChild(page, "backgroundAnalysisButton")
        const preview = findChild(page, "backgroundPreview")
        const error = findChild(page, "backgroundImportError")
        verify(importButton.enabled)
        verify(analysis.visible && !analysis.enabled)
        verify(preview.readOnly && preview.selectByMouse)
        compare(preview.textFormat, TextEdit.PlainText)
        compare(preview.text, "")
        verify(preview.placeholderText.length > 0)
        verify(!error.visible)

        picker.selectedFile = validFile
        picker.accepted()
        compare(String(document.importedFile), String(validFile))
        compare(preview.text, "# 项目背景\n\n**重点**\n\n<p>保留 Markdown 源码。</p>")
        verify(analysis.visible && !analysis.enabled)

        picker.selectedFile = invalidFile
        picker.rejected()
        compare(String(document.importedFile), String(validFile))
        picker.accepted()
        compare(String(document.importedFile), String(invalidFile))
        verify(error.visible)
        compare(error.text, "Markdown 文件内容无效。")
        verify(preview.text.startsWith("# 项目背景"))
    }
    function test_backgroundAnalysisStartsStreamsAndStops() {
        const document = createTemporaryObject(backgroundDocumentFactory, testCase)
        const controller = createTemporaryObject(backgroundAnalysisFactory, testCase)
        const backgroundApp = createTemporaryObject(factory, null, {
            backgroundDocument: document,
            backgroundAnalysis: controller
        })
        verify(backgroundApp !== null)
        backgroundApp.width = 760
        backgroundApp.height = 520
        backgroundApp.demoController.route = "background"
        const loader = findChild(backgroundApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const page = loader.item
        const importButton = findChild(page, "backgroundImportButton")
        const analysis = findChild(page, "backgroundAnalysisButton")
        const preview = findChild(page, "backgroundPreview")

        controller.configured = true
        verify(!analysis.enabled)
        controller.configured = false
        document.backgroundText = "# 项目背景"
        verify(!analysis.enabled)
        controller.configured = true
        verify(analysis.enabled)
        mouseClick(analysis)
        verify(controller.running)
        compare(analysis.text, "停止分析")
        verify(analysis.enabled)
        verify(!importButton.enabled)
        const result = findChild(page, "backgroundAnalysisResult")
        const status = findChild(page, "backgroundAnalysisStatus")
        verify(result.visible && result.readOnly && result.selectByMouse)
        compare(result.textFormat, TextEdit.PlainText)
        compare(status.text, "正在分析…")

        controller.resultText = "第一行"
        compare(result.text, "第一行")
        result.select(0, 2)
        controller.resultText += "\n<b>保留原文</b>"
        compare(result.text, "第一行\n<b>保留原文</b>")
        compare(result.selectedText, "第一")
        compare(preview.text, "# 项目背景")
        mouseClick(analysis)
        verify(!controller.running)
        compare(analysis.text, "AI 分析")
        verify(importButton.enabled)
        compare(status.text, "分析已停止，以下为已有内容。")
        compare(result.text, "第一行\n<b>保留原文</b>")

        mouseClick(analysis)
        verify(controller.running)
        controller.resultText = "分析完成。"
        controller.running = false
        compare(result.text, "分析完成。")
        compare(status.text, "")
        compare(analysis.text, "AI 分析")
        verify(importButton.enabled)

        backgroundApp.demoController.route = "home"
        tryCompare(loader, "status", Loader.Ready)
        backgroundApp.demoController.route = "background"
        tryCompare(loader, "status", Loader.Ready)
        const reopenedResult = findChild(loader.item, "backgroundAnalysisResult")
        verify(reopenedResult.visible)
        compare(reopenedResult.text, "分析完成。")
    }
    function test_modelConnectionWithoutControllerDisablesActions() {
        app.demoController.route = "model"
        const loader = findChild(app, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const endpoint = findChild(loader.item, "endpointField")
        verify(endpoint.readOnly)
        verify(!findChild(loader.item, "testConnectionButton").enabled)
        compare(findChild(loader.item, "saveModelButton"), null)
    }
    function test_modelConnectionSavesModelOnSelectionAndKeyAfterEditing() {
        const connection = createTemporaryObject(modelConnectionFactory, testCase)
        const modelApp = createTemporaryObject(factory, null, { modelConnection: connection })
        verify(modelApp !== null)
        modelApp.width = 760
        modelApp.height = 520
        modelApp.demoController.route = "model"
        const loader = findChild(modelApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const page = loader.item
        const endpoint = findChild(page, "endpointField")
        const key = findChild(page, "apiKeyField")
        const model = findChild(page, "modelNameField")
        const testButton = findChild(page, "testConnectionButton")

        compare(endpoint.text, "https://api.deepseek.com")
        compare(key.echoMode, TextInput.Password)
        compare(model.currentText, "deepseek-flash")
        verify(!testButton.enabled)

        mouseClick(model)
        tryCompare(model.popup, "opened", true)
        mouseClick(model.popup.contentItem.itemAtIndex(1))
        compare(model.currentText, "deepseek-v4-pro")
        compare(connection.draftModelName, "deepseek-v4-pro")
        compare(connection.savedModelName, "deepseek-v4-pro")
        compare(findChild(page, "saveModelButton"), null)
        const status = findChild(page, "modelVerificationStatus")
        compare(status.text, "未配置 API Key")

        modelApp.requestActivate()
        tryCompare(modelApp, "active", true)
        mouseClick(key)
        keyClick(Qt.Key_A)
        compare(connection.draftApiKey, "a")
        compare(connection.savedApiKey, "")
        compare(status.text, "等待保存")
        mouseClick(endpoint)
        tryCompare(key, "activeFocus", false)
        compare(connection.savedApiKey, "a")
        compare(connection.verificationCount, 1)
        compare(status.text, "检测中…")
        verify(!testButton.enabled)

        mouseClick(key)
        keyClick(Qt.Key_B)
        keyClick(Qt.Key_Return)
        compare(connection.savedApiKey, "ab")
        compare(connection.verificationCount, 2)
        compare(status.text, "检测中…")
        connection.verificationStatus = "passed"
        compare(status.text, "API 连接正常")

        mouseClick(model)
        tryCompare(model.popup, "opened", true)
        mouseClick(model.popup.contentItem.itemAtIndex(0))
        compare(connection.savedModelName, "deepseek-flash")
        compare(connection.verificationCount, 2)
        compare(status.text, "API 连接正常")

        mouseClick(key)
        key.selectAll()
        keyClick(Qt.Key_Backspace)
        keyClick(Qt.Key_Return)
        compare(connection.savedApiKey, "")
        compare(connection.verificationCount, 2)
        compare(status.text, "未配置 API Key")
        verify(!testButton.enabled)
    }
    function test_modelConnectionChecksInlineAndRetries() {
        const connection = createTemporaryObject(modelConnectionFactory, testCase, {
            draftApiKey: "test-key"
        })
        const modelApp = createTemporaryObject(factory, null, { modelConnection: connection })
        verify(modelApp !== null)
        modelApp.width = 760
        modelApp.height = 520
        modelApp.demoController.route = "model"
        const loader = findChild(modelApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const testButton = findChild(loader.item, "testConnectionButton")
        const status = findChild(loader.item, "modelVerificationStatus")
        compare(status.text, "等待保存")
        mouseClick(testButton)
        compare(connection.savedApiKey, "test-key")
        compare(connection.verificationCount, 1)
        compare(status.text, "检测中…")
        compare(findChild(modelApp, "modelConnectionTestDialog"), null)
        verify(!testButton.enabled)
        mouseClick(testButton)
        compare(connection.verificationCount, 1)

        connection.verificationMessage = "DeepSeek API Key 无效，请检查后重试。"
        connection.verificationStatus = "failed"
        compare(status.text, "DeepSeek API Key 无效，请检查后重试。")
        verify(testButton.enabled)
        mouseClick(testButton)
        compare(connection.verificationCount, 2)
        compare(status.text, "检测中…")
        connection.verificationStatus = "passed"
        compare(status.text, "API 连接正常")
        verify(testButton.enabled)
    }
    function test_modelChatStreamsAndKeepsRestCheckIndependent() {
        const connection = createTemporaryObject(modelConnectionFactory, testCase, {
            savedApiKey: "test-key",
            draftApiKey: "test-key",
            savedModelName: "deepseek-v4-pro",
            verificationStatus: "failed",
            verificationMessage: "模型列表检测失败，请重试。"
        })
        const chat = createTemporaryObject(modelTestChatFactory, testCase)
        const modelApp = createTemporaryObject(factory, null, { modelConnection: connection, modelTestChat: chat })
        verify(modelApp !== null)
        modelApp.width = 760
        modelApp.height = 520
        modelApp.demoController.route = "model"
        const loader = findChild(modelApp, "pageLoader")
        tryCompare(loader, "status", Loader.Ready)
        const chatButton = findChild(loader.item, "testChatButton")
        const apiStatus = findChild(loader.item, "modelVerificationStatus")
        verify(chatButton !== null && chatButton.enabled)
        modelApp.requestActivate()
        tryCompare(modelApp, "active", true)
        mouseClick(chatButton)
        const dialog = findChild(modelApp, "modelTestChatDialog")
        verify(dialog !== null)
        tryCompare(dialog, "opened", true)
        compare(chat.beginCount, 1)
        compare(chat.turns.count, 0)
        verify(!chat.running)
        verify(dialog.width < modelApp.width && dialog.height < modelApp.height)
        compare(findChild(dialog, "modelTestChatModel").text, "模型：deepseek-v4-pro")
        const prompt = findChild(dialog, "modelTestChatPrompt")
        const send = findChild(dialog, "modelTestChatSendButton")
        tryCompare(prompt, "activeFocus", true)
        verify(!send.enabled)
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_Return, Qt.ShiftModifier)
        keyClick(Qt.Key_B)
        compare(prompt.text, "a\nb")
        testInputMethod.sendInputMethodText(prompt, "zhong", "")
        verify(prompt.inputMethodComposing)
        keyClick(Qt.Key_Return)
        compare(chat.turns.count, 0)
        testInputMethod.sendInputMethodText(prompt, "", "中")
        verify(!prompt.inputMethodComposing)
        const committedPrompt = prompt.text
        verify(committedPrompt.includes("中"))
        keyClick(Qt.Key_Return)
        compare(chat.turns.count, 1)
        compare(chat.turns.get(0).promptText, committedPrompt)
        compare(prompt.text, "")
        verify(chat.running && !send.enabled)
        tryCompare(prompt, "activeFocus", true)
        keyClick(Qt.Key_C)
        keyClick(Qt.Key_Return)
        compare(chat.turns.count, 1)
        compare(prompt.text, "c")

        const turns = findChild(dialog, "modelTestChatTurns")
        const response = findChild(turns.itemAt(0), "modelTestChatResponse")
        chat.updateReply("第一行")
        compare(response.text, "第一行")
        chat.updateReply("第一行\n<b>原文</b>")
        compare(response.text, "第一行\n<b>原文</b>")
        compare(response.textFormat, TextEdit.PlainText)
        verify(response.readOnly && response.selectByMouse)
        const viewport = findChild(dialog, "modelTestChatTimeline").contentItem
        chat.updateReply(response.text + "\n后续回复".repeat(25))
        chat.updateReply(response.text + "\n最后一行仍然可见。")
        verify(waitForRendering(response))
        tryVerify(() => {
            const tail = response.positionToRectangle(response.length)
            const bottom = response.mapToItem(viewport, tail.x, tail.y + tail.height).y
            return bottom > 0 && bottom <= viewport.height - response.bottomPadding + 1
        })
        viewport.contentY = viewport.originY
        response.select(0, 3)
        const previousHeight = viewport.contentHeight
        chat.updateReply(response.text + "\n正在追加".repeat(6))
        tryVerify(() => viewport.contentHeight > previousHeight)
        compare(viewport.contentY, viewport.originY)
        compare(response.selectedText, "第一行")

        chat.finishTurn("complete", "")
        verify(send.enabled)
        mouseClick(send)
        compare(chat.turns.count, 2)
        compare(chat.turns.get(1).promptText, "c")
        verify(response.text.startsWith("第一行"))
        chat.updateReply("部分回复")
        chat.finishTurn("failed", "请求失败，请重试。")
        const second = turns.itemAt(1)
        compare(findChild(second, "modelTestChatStatus").text, "请求失败，请重试。")
        compare(findChild(second, "modelTestChatResponse").text, "部分回复")
        const retry = findChild(second, "modelTestChatRetryButton")
        verify(retry.visible)
        tryVerify(() => {
            const top = retry.mapToItem(viewport, 0, 0).y
            return top >= 0 && top + retry.height <= viewport.height + 1
        })
        mouseClick(retry)
        verify(chat.running)
        compare(chat.turns.count, 2)

        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "opened", false)
        verify(!chat.running)
        compare(chat.turns.count, 0)
        compare(chat.modelName, "")
        compare(connection.verificationCount, 0)
        compare(apiStatus.text, "模型列表检测失败，请重试。")
        mouseClick(chatButton)
        tryCompare(dialog, "opened", true)
        compare(chat.beginCount, 2)
        compare(prompt.text, "")
        compare(chat.turns.count, 0)
        mouseClick(findChild(dialog, "modelTestChatCloseButton"))
        tryCompare(dialog, "opened", false)
        compare(chat.modelName, "")
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
