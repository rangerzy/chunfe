import QtQuick
import "DemoData.js" as Data

QtObject {
    id: root
    objectName: "demoController"

    property var conversationStore: null
    property var offlineTranscription: null
    property var audioLevelTest: null
    property var conversationAdvice: null
    readonly property bool isDemo: conversationStore === null
    property string route: "home"
    property string backgroundText: ""
    readonly property string modelName: "gpt-4.1"
    property string engineStatus: "ready"
    property string systemDevice: "系统默认输出"
    property string microphone: "系统默认麦克风"
    property string startScenario: "success"
    property var demoRecords: isDemo ? Data.records() : []
    property var unsavedRecords: []
    readonly property var records: isDemo ? demoRecords
                                          : unsavedRecords.concat(Array.from(conversationStore.records ?? []))
    property string selectedRecordId: ""
    readonly property var selectedRecord: {
        const summary = records.find(item => item.id === selectedRecordId);
        if (!summary)
            return null;
        if (isDemo || summary.unsaved)
            return summary;
        const detail = conversationStore.record(selectedRecordId);
        return detail?.id ? detail : null;
    }

    property bool preflightVisible: false
    property string checkState: "idle"
    property int checkStep: 0
    property string nativeCheckMessage: ""
    property bool nativeStarting: false
    property bool nativeStartCallActive: false
    readonly property bool canLoadConversationEngine: !isDemo && !!offlineTranscription
                                                      && offlineTranscription.engineStatus !== "ready"
    property int checkInterval: 320
    property int streamInterval: 45
    property bool interviewActive: false
    property bool interviewEnding: false
    property bool paused: false
    property bool transcriptCollapsed: false
    property bool answerCollapsed: false
    property int elapsedSeconds: 0
    readonly property string elapsedText: formatTime(elapsedSeconds)
    property var messages: []
    readonly property var liveConversationMessages: !isDemo && interviewActive
                                                     ? transcriptMessages(offlineTranscription?.conversationLines ?? []) : []
    readonly property bool nativeAdviceListening: !isDemo && interviewActive && !paused && !interviewEnding
    onNativeAdviceListeningChanged: {
        if (!isDemo)
            conversationAdvice?.setListening(nativeAdviceListening);
    }
    readonly property string nativeConversationStatus: {
        if (interviewEnding)
            return qsTr("正在结束转写…");
        if (offlineTranscription?.errorMessage)
            return offlineTranscription.errorMessage;
        switch (offlineTranscription?.status) {
        case "pausing": return qsTr("正在暂停…");
        case "paused": return qsTr("转写已暂停");
        case "resuming": return qsTr("正在恢复…");
        default: return qsTr("正在实时转写");
        }
    }
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
    property real sessionStartedAt: 0
    property string sessionDate: ""
    property string sessionBackground: ""
    property string sessionModel: ""
    property int sessionSequence: 0
    readonly property var samples: isDemo ? Data.questions() : []

    property Connections nativeUpdates: Connections {
        target: root.isDemo ? null : root.offlineTranscription
        ignoreUnknownSignals: true
        function onChanged() { root.observeNativeStart(); }
        function onStatusChanged() { root.observeNativeStart(); }
        function onLinesChanged() { root.forwardAdviceTranscript(); }
        function onConversationFinished(lines) { root.finishNativeConversation(lines); }
    }
    property Connections nativeAudioUpdates: Connections {
        target: root.isDemo ? null : root.audioLevelTest
        ignoreUnknownSignals: true
        function onChanged() { root.continueNativeChecks(); }
        function onStoppingChanged() { root.continueNativeChecks(); }
    }

    property TranscriptionTestController transcriptionTest: TranscriptionTestController {
        engineStatus: root.engineStatus
        spkDevice: root.systemDevice
        micDevice: root.microphone
        allowTesting: root.route === "audio" && !root.interviewActive && !root.preflightVisible
        onStartEngineRequested: root.startEngine()
    }

    signal notification(string message, string severity)
    signal interviewStarted
    signal interviewFinished

    function notify(message: string, severity: string) {
        notification(message, severity);
    }
    function formatTime(seconds: int): string {
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0");
    }
    function saveAudio(system: string, mic: string) {
        systemDevice = system;
        microphone = mic;
        notify(qsTr("音频设备已更新。"), "success");
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
        const name = title.trim();
        if (!name) {
            notify(qsTr("名称不能为空。"), "error");
            return;
        }
        const withNewTitle = item => item.id === id ? Object.assign({}, item, { title: name }) : item;
        if (isDemo)
            demoRecords = demoRecords.map(withNewTitle);
        else if (unsavedRecords.some(item => item.id === id)) {
            unsavedRecords = unsavedRecords.map(withNewTitle);
            return;
        } else if (!conversationStore.renameRecord(id, name)) {
            notify(conversationStore.errorMessage || qsTr("无法重命名对话记录。"), "error");
            return;
        }
        notify(qsTr("名称已更新。"), "success");
    }
    function deleteRecord(id: string) {
        if (isDemo)
            demoRecords = demoRecords.filter(item => item.id !== id);
        else if (unsavedRecords.some(item => item.id === id))
            unsavedRecords = unsavedRecords.filter(item => item.id !== id);
        else if (!conversationStore.deleteRecord(id)) {
            notify(conversationStore.errorMessage || qsTr("无法删除对话记录。"), "error");
            return;
        }
        if (selectedRecordId === id) {
            selectedRecordId = "";
            route = "history";
        }
        notify(qsTr("记录已删除。"), "success");
    }
    function retryRecordSave(id: string): bool {
        const unsaved = unsavedRecords.find(item => item.id === id);
        if (!unsaved || !conversationStore)
            return false;
        const record = Object.assign({}, unsaved);
        delete record.unsaved;
        if (!conversationStore.saveRecord(record)) {
            notify(conversationStore.errorMessage || qsTr("无法保存对话记录。"), "error");
            return false;
        }
        unsavedRecords = unsavedRecords.filter(item => item.id !== id);
        notify(qsTr("对话记录已保存。"), "success");
        return true;
    }
    function exportRecord(id: string) {
        if (isDemo && records.some(function (item) {
            return item.id === id;
        }))
            notify(qsTr("Markdown 内容已准备。"), "info");
    }
    function startChecks() {
        if (interviewActive || (!isDemo && preflightVisible && checkState === "checking"))
            return;
        if (!isDemo) {
            checkState = "checking";
            nativeCheckMessage = qsTr("正在准备实时转写…");
            preflightVisible = true;
            if (offlineTranscription?.opened && !offlineTranscription.conversationActive)
                offlineTranscription.close();
            if (audioLevelTest?.active)
                audioLevelTest.stop();
            continueNativeChecks();
            return;
        }
        transcriptionTest.close();
        checkTimer.stop();
        checkStep = 0;
        checkState = "checking";
        preflightVisible = true;
        checkTimer.start();
    }
    function cancelChecks() {
        const abortStart = nativeStarting;
        nativeStarting = false;
        checkTimer.stop();
        preflightVisible = false;
        checkState = "idle";
        checkStep = 0;
        nativeCheckMessage = "";
        if (abortStart && offlineTranscription?.conversationActive)
            offlineTranscription.abortConversation();
    }
    function retryChecks() {
        if (isDemo)
            startScenario = "success";
        startChecks();
    }
    function openConversationEngineSettings() {
        cancelChecks();
        route = "offline";
    }
    function failNativeChecks(message: string) {
        nativeStarting = false;
        checkState = "failed";
        nativeCheckMessage = message;
    }
    function continueNativeChecks() {
        if (isDemo || !preflightVisible || checkState !== "checking" || nativeStarting)
            return;
        if (!offlineTranscription) {
            failNativeChecks(qsTr("离线转写服务不可用。"));
            return;
        }
        if (offlineTranscription.engineStatus !== "ready") {
            failNativeChecks(offlineTranscription.engineError || qsTr("请先加载离线转写模型。"));
            return;
        }
        if (audioLevelTest?.active || audioLevelTest?.stopping || offlineTranscription.opened) {
            nativeCheckMessage = qsTr("正在等待音频测试释放设备…");
            return;
        }
        nativeCheckMessage = qsTr("正在启动实时转写…");
        nativeStarting = true;
        nativeStartCallActive = true;
        const accepted = offlineTranscription.startConversation();
        nativeStartCallActive = false;
        if (!accepted)
            failNativeChecks(offlineTranscription.errorMessage || qsTr("无法启动实时转写。"));
        observeNativeStart();
    }
    function observeNativeStart() {
        if (nativeStartCallActive)
            return;
        if (interviewActive && !interviewEnding) {
            const state = offlineTranscription.status;
            if (state === "pausing" || state === "paused")
                paused = true;
            else if (state === "running" || state === "resuming")
                paused = false;
        } else if (nativeStarting) {
            if (offlineTranscription.conversationActive && offlineTranscription.status === "running") {
                nativeStarting = false;
                const name = (offlineTranscription.modelPath || "").split(/[\\/]/).pop();
                startInterview(conversationStore.newRecordId(), name || qsTr("离线转写"));
            } else if (offlineTranscription.status === "error") {
                failNativeChecks(offlineTranscription.errorMessage || qsTr("无法启动实时转写。"));
            }
        } else {
            continueNativeChecks();
        }
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
        if (!isDemo) {
            startChecks();
            return;
        }
        transcriptionTest.close();
        sessionSequence++;
        startInterview("session-" + Date.now() + "-" + sessionSequence, modelName);
    }
    function startInterview(id: string, engine: string) {
        cancelChecks();
        sessionId = id;
        sessionStartedAt = Date.now();
        sessionDate = Qt.formatDateTime(new Date(sessionStartedAt), "yyyy-MM-dd hh:mm");
        sessionBackground = backgroundText;
        sessionModel = engine;
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
        interviewEnding = false;
        transcriptCollapsed = false;
        answerCollapsed = false;
        if (!isDemo)
            conversationAdvice?.beginSession(sessionBackground);
        interviewActive = true;
        if (isDemo)
            queueQuestion(0);
        else
            forwardAdviceTranscript();
        interviewStarted();
    }
    function forwardAdviceTranscript() {
        if (!isDemo && interviewActive && conversationAdvice)
            conversationAdvice.considerTranscript(offlineTranscription?.conversationLines ?? []);
    }
    function togglePause() {
        if (!interviewActive || interviewEnding)
            return;
        const desired = !paused;
        if (isDemo || offlineTranscription.setConversationPaused(desired))
            paused = desired;
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
        if (!interviewActive || paused || interviewEnding)
            return;
        elapsedSeconds++;
        if (!isDemo)
            return;
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
        if (!interviewActive || interviewEnding)
            return;
        if (!isDemo) {
            interviewEnding = true;
            if (offlineTranscription?.conversationActive)
                offlineTranscription.stopConversation();
            else
                finishNativeConversation(offlineTranscription?.lines ?? []);
            return;
        }
        completeInterview(messages.slice(), currentQuestions.slice());
    }
    function transcriptMessages(lines) {
        return Array.from(lines).map(line => ({
            id: line.id,
            source: line.source,
            role: line.source === "mic" ? "me" : "other",
            time: line.time,
            text: line.text,
            final: !!line.final,
            incomplete: !!line.incomplete
        }));
    }
    function finishNativeConversation(lines) {
        if (!interviewActive)
            return;
        interviewEnding = true;
        const questions = Array.from(conversationAdvice?.finishSession() ?? []);
        completeInterview(transcriptMessages(lines), questions);
    }
    function completeInterview(transcript, questions) {
        interviewActive = false;
        interviewEnding = false;
        paused = false;
        const record = {
            id: sessionId,
            title: qsTr("对话 · %1").arg(sessionDate),
            date: sessionDate,
            startedAt: sessionStartedAt,
            duration: elapsedSeconds < 60 ? elapsedSeconds + " 秒" : Math.floor(elapsedSeconds / 60) + " 分钟",
            model: sessionModel,
            isDemo: isDemo,
            questions: questions,
            transcript: transcript,
            backgroundText: sessionBackground
        };
        let failure = "";
        if (isDemo) {
            demoRecords = [record].concat(demoRecords);
        } else if (!conversationStore.saveRecord(record)) {
            failure = conversationStore.errorMessage || qsTr("无法保存对话记录。");
            unsavedRecords = [Object.assign({}, record, { unsaved: true })].concat(unsavedRecords);
        } else {
            failure = offlineTranscription?.errorMessage ?? "";
        }
        selectedRecordId = sessionId;
        route = "record";
        pendingTicks = 0;
        targetAnswer = "";
        pendingQuestion = "";
        pendingTriggerKind = "";
        interviewFinished();
        if (failure)
            notify(failure, "error");
        else
            notify(qsTr("对话已结束。"), "success");
    }

    property Timer checkTimer: Timer {
        interval: root.checkInterval
        repeat: true
        onTriggered: root.advanceCheck()
    }
    property Timer engineTimer: Timer {
        interval: 950
        onTriggered: {
            if (root.engineStatus !== "starting")
                return;
            root.engineStatus = "ready";
            if (root.route !== "audio")
                root.notify(qsTr("转写引擎已就绪。"), "success");
        }
    }
    property Timer sessionTimer: Timer {
        interval: 1000
        repeat: true
        running: root.interviewActive && !root.paused && !root.interviewEnding
        onTriggered: root.advanceSession()
    }
    property Timer streamTimer: Timer {
        interval: root.streamInterval
        repeat: true
        running: root.generating && !root.paused
        onTriggered: root.advanceAnswer()
    }
}
