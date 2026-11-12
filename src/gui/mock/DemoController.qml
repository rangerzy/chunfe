import QtQuick
import "DemoData.js" as Data

QtObject {
    id: root
    objectName: "demoController"

    readonly property bool isDemo: true
    property string route: "home"
    property string backgroundText: ""
    property string backgroundName: ""
    property string backgroundDraft: ""
    readonly property bool backgroundDirty: backgroundDraft !== backgroundText
    property string apiEndpoint: "https://api.example.com/v1"
    property string apiKey: ""
    property string modelName: "gpt-4.1"
    property string connectionStatus: "idle"
    property string engineStatus: "ready"
    property string systemDevice: "系统默认输出"
    property string microphone: "系统默认麦克风"
    property string startScenario: "success"
    property var records: Data.records()
    property string selectedRecordId: ""
    readonly property var selectedRecord: records.find(function (item) {
        return item.id === root.selectedRecordId;
    }) || null

    property bool preflightVisible: false
    property string checkState: "idle"
    property int checkStep: 0
    property int checkInterval: 320
    property int streamInterval: 45
    property bool interviewActive: false
    property bool paused: false
    property bool transcriptCollapsed: false
    property bool answerCollapsed: false
    property int elapsedSeconds: 0
    readonly property string elapsedText: formatTime(elapsedSeconds)
    property var messages: []
    property string question: ""
    property string answer: ""
    property string pendingQuestion: ""
    property string triggerKind: ""
    property string pendingTriggerKind: ""
    readonly property bool generating: interviewActive && (pendingTicks > 0 || streamCursor < targetAnswer.length)
    property var currentQuestions: []
    property int questionIndex: -1
    property int repliedQuestionIndex: -1
    property int pendingTicks: 0
    property int streamCursor: 0
    property string targetAnswer: ""
    property string sessionId: ""
    property string sessionDate: ""
    property string sessionBackground: ""
    property string sessionModel: ""
    property int sessionSequence: 0
    readonly property var samples: Data.questions()

    property TranscriptionTestController transcriptionTest: TranscriptionTestController {
        engineStatus: root.engineStatus
        spkDevice: root.systemDevice
        micDevice: root.microphone
        allowTesting: root.route === "audio" && !root.interviewActive && !root.preflightVisible
        onStartEngineRequested: root.startEngine()
    }

    signal notification(string message)
    signal interviewStarted
    signal interviewFinished

    function notify(message: string) {
        notification(message);
    }
    function formatTime(seconds: int): string {
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0");
    }
    function saveBackground(text: string) {
        backgroundText = text.trim();
        backgroundDraft = backgroundText;
        backgroundName = backgroundText ? "背景资料.md" : "";
        notify("背景已保存。");
    }
    function importMockBackground() {
        backgroundDraft = Data.background();
        notify("背景已导入，请保存。");
    }
    function clearBackground() {
        backgroundText = "";
        backgroundDraft = "";
        backgroundName = "";
        notify("背景已清空。");
    }
    function saveModel(endpoint: string, key: string, model: string) {
        if (!endpoint.trim() || !model.trim()) {
            notify("请填写 API 地址和模型名称。");
            return;
        }
        apiEndpoint = endpoint.trim();
        apiKey = key;
        modelName = model.trim();
        connectionStatus = "idle";
        connectionTimer.stop();
        notify("模型配置已保存。");
    }
    function testConnection() {
        connectionStatus = "testing";
        connectionTimer.restart();
    }
    function saveAudio(system: string, mic: string) {
        systemDevice = system;
        microphone = mic;
        notify("音频设备已更新。");
    }
    function startEngine() {
        if (engineStatus === "starting" || engineStatus === "ready")
            return;
        engineTimer.stop();
        engineStatus = "starting";
        engineTimer.restart();
    }
    function toggleEngine() {
        if (engineStatus === "starting")
            return;
        if (engineStatus === "ready") {
            engineTimer.stop();
            engineStatus = "stopped";
        } else
            startEngine();
    }
    function openRecord(id: string) {
        selectedRecordId = id;
        route = "record";
    }
    function renameRecord(id: string, title: string) {
        if (!title.trim()) {
            notify("名称不能为空。");
            return;
        }
        records = records.map(function (item) {
            return item.id === id ? Object.assign({}, item, {
                title: title.trim()
            }) : item;
        });
        notify("名称已更新。");
    }
    function deleteRecord(id: string) {
        records = records.filter(function (item) {
            return item.id !== id;
        });
        if (selectedRecordId === id) {
            selectedRecordId = "";
            route = "history";
        }
        notify("记录已删除。");
    }
    function exportRecord(id: string) {
        if (records.some(function (item) {
            return item.id === id;
        }))
            notify("Markdown 内容已准备。");
    }
    function startChecks() {
        if (interviewActive)
            return;
        transcriptionTest.close();
        checkTimer.stop();
        checkStep = 0;
        checkState = "checking";
        preflightVisible = true;
        checkTimer.start();
    }
    function cancelChecks() {
        checkTimer.stop();
        preflightVisible = false;
        checkState = "idle";
        checkStep = 0;
    }
    function retryChecks() {
        startScenario = "success";
        startChecks();
    }
    function advanceCheck() {
        if (!preflightVisible || checkState !== "checking")
            return;
        checkStep++;
        if (checkStep === 1) {
            engineTimer.stop();
            engineStatus = startScenario === "engine" ? "error" : "ready";
        }
        if (checkStep >= 5 && startScenario !== "success") {
            checkState = "failed";
            checkTimer.stop();
        } else if (checkStep >= 6)
            beginInterview();
    }
    function beginInterview() {
        if (interviewActive)
            return;
        transcriptionTest.close();
        cancelChecks();
        sessionSequence++;
        sessionId = "session-" + Date.now() + "-" + sessionSequence;
        sessionDate = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm");
        sessionBackground = backgroundText;
        sessionModel = modelName;
        messages = [];
        currentQuestions = [];
        question = "";
        answer = "";
        pendingQuestion = "";
        triggerKind = "";
        pendingTriggerKind = "";
        targetAnswer = "";
        streamCursor = 0;
        pendingTicks = 0;
        questionIndex = -1;
        repliedQuestionIndex = -1;
        elapsedSeconds = 0;
        paused = false;
        transcriptCollapsed = false;
        answerCollapsed = false;
        interviewActive = true;
        queueQuestion(0);
        interviewStarted();
    }
    function togglePause() {
        if (interviewActive)
            paused = !paused;
    }
    function queueQuestion(index: int) {
        if (!interviewActive || paused || index < 0 || index >= samples.length)
            return;
        questionIndex = index;
        const sample = samples[index];
        messages = messages.concat([
            {
                role: "other",
                text: sample.question,
                time: elapsedText
            }
        ]);
        // Ordinary statements keep both the current and pending suggestion intact.
        if (!["question", "request", "objection"].includes(sample.kind))
            return;
        pendingQuestion = sample.question;
        pendingTriggerKind = sample.kind;
        targetAnswer = sample.answer;
        pendingTicks = 12;
        streamCursor = 0;
    }
    function advanceAnswer() {
        if (!interviewActive || paused)
            return;
        if (pendingTicks > 0) {
            pendingTicks--;
            if (pendingTicks === 0) {
                question = pendingQuestion;
                pendingQuestion = "";
                answer = "";
                triggerKind = pendingTriggerKind;
                pendingTriggerKind = "";
                currentQuestions = currentQuestions.concat([
                    {
                        kind: triggerKind,
                        question: question,
                        answer: ""
                    }
                ]);
            }
            return;
        }
        streamCursor = Math.min(targetAnswer.length, streamCursor + 4);
        answer = targetAnswer.slice(0, streamCursor);
        const next = currentQuestions.slice();
        if (next.length)
            next[next.length - 1] = {
                kind: triggerKind,
                question: question,
                answer: answer
            };
        currentQuestions = next;
    }
    function advanceSession() {
        if (!interviewActive || paused)
            return;
        elapsedSeconds++;
        if (elapsedSeconds % 18 === 8 && questionIndex >= 0 && repliedQuestionIndex !== questionIndex && samples[questionIndex].reply) {
            messages = messages.concat([
                {
                    role: "me",
                    text: samples[questionIndex].reply,
                    time: elapsedText
                }
            ]);
            repliedQuestionIndex = questionIndex;
        }
        if (elapsedSeconds % 18 === 0 && questionIndex + 1 < samples.length)
            queueQuestion(questionIndex + 1);
    }
    function finishInterview() {
        if (!interviewActive)
            return;
        interviewActive = false;
        paused = false;
        // Snapshot before resetting live state; prior sessions never share mutable arrays.
        const record = {
            id: sessionId,
            title: "对话 · " + sessionDate,
            date: sessionDate,
            duration: elapsedSeconds < 60 ? elapsedSeconds + " 秒" : Math.floor(elapsedSeconds / 60) + " 分钟",
            model: sessionModel,
            isDemo: true,
            questions: currentQuestions.slice(),
            transcript: messages.slice(),
            backgroundText: sessionBackground
        };
        records = [record].concat(records);
        selectedRecordId = sessionId;
        route = "record";
        pendingTicks = 0;
        targetAnswer = "";
        pendingQuestion = "";
        pendingTriggerKind = "";
        interviewFinished();
        notify("对话已结束。");
    }

    property Timer checkTimer: Timer {
        interval: root.checkInterval
        repeat: true
        onTriggered: root.advanceCheck()
    }
    property Timer connectionTimer: Timer {
        interval: 850
        onTriggered: {
            root.connectionStatus = "connected";
            root.notify("连接成功。");
        }
    }
    property Timer engineTimer: Timer {
        interval: 950
        onTriggered: {
            if (root.engineStatus !== "starting")
                return;
            root.engineStatus = "ready";
            root.notify("转写引擎已就绪。");
        }
    }
    property Timer sessionTimer: Timer {
        interval: 1000
        repeat: true
        running: root.interviewActive && !root.paused
        onTriggered: root.advanceSession()
    }
    property Timer streamTimer: Timer {
        interval: root.streamInterval
        repeat: true
        running: root.generating && !root.paused
        onTriggered: root.advanceAnswer()
    }
}
