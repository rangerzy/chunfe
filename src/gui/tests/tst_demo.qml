import QtQuick
import QtTest
import "../mock" as Mock

TestCase {
    id: testCase
    name: "DemoLifecycle"
    when: windowShown
    Component {
        id: factory
        Mock.DemoController {
            checkInterval: 5
            streamInterval: 5
        }
    }
    property var demo
    function init() {
        demo = createTemporaryObject(factory, testCase);
    }
    function cleanup() {
        demo.cancelChecks();
        demo.finishInterview();
    }
    function test_cancelDoesNotLaunch() {
        demo.startChecks();
        demo.cancelChecks();
        wait(80);
        verify(!demo.interviewActive);
        verify(!demo.preflightVisible);
    }
    function test_automaticEntryAndReturnRecord() {
        demo.startChecks();
        tryCompare(demo, "interviewActive", true);
        verify(!demo.preflightVisible);
        demo.finishInterview();
        compare(demo.route, "record");
        verify(demo.selectedRecord !== null);
        compare(demo.interviewActive, false);
    }
    function test_failedChecksStayUntilRetry() {
        demo.startScenario = "engine";
        demo.startChecks();
        tryCompare(demo, "checkState", "failed");
        verify(!demo.interviewActive);
        verify(demo.preflightVisible);
        demo.retryChecks();
        tryCompare(demo, "interviewActive", true);
    }
    function test_pauseFreezesTranscriptAndAnswer() {
        demo.beginInterview();
        demo.togglePause();
        const count = demo.messages.length;
        const answer = demo.answer;
        wait(100);
        compare(demo.messages.length, count);
        compare(demo.answer, answer);
        demo.togglePause();
        tryVerify(function () {
            return demo.answer.length > answer.length;
        });
    }
    function test_newQuestionPreservesOldUntilAnswerStarts() {
        demo.beginInterview();
        tryVerify(function () {
            return demo.answer.length > 10;
        });
        const oldQuestion = demo.question;
        const oldAnswer = demo.answer;
        demo.queueQuestion(2);
        compare(demo.question, oldQuestion);
        compare(demo.answer, oldAnswer);
        verify(demo.pendingQuestion.length > 0);
        tryVerify(function () {
            return demo.question !== oldQuestion;
        });
        compare(demo.pendingQuestion, "");
    }
    function test_backgroundSnapshotAndSessionIsolation() {
        demo.saveBackground("第一版背景");
        demo.beginInterview();
        demo.finishInterview();
        const firstId = demo.selectedRecord.id;
        compare(demo.selectedRecord.backgroundText, "第一版背景");
        demo.saveBackground("第二版背景");
        demo.beginInterview();
        compare(demo.messages.length, 1);
        demo.finishInterview();
        compare(demo.selectedRecord.backgroundText, "第二版背景");
        demo.openRecord(firstId);
        compare(demo.selectedRecord.backgroundText, "第一版背景");
        demo.deleteRecord(firstId);
        compare(demo.backgroundText, "第二版背景");
    }
    function test_lastReplyIsNotRepeated() {
        demo.beginInterview();
        for (let i = 0; i < 98; ++i)
            demo.advanceSession();
        compare(demo.messages.length, 8);
        compare(demo.messages.filter(function (message) {
            return message.role === "me";
        }).length, 3);
        verify(demo.messages.every(function (message) {
            return typeof message.text === "string" && message.text.length > 0;
        }));
    }
    function completeSuggestion() {
        for (let i = 0; i < 1000 && demo.generating; ++i)
            demo.advanceAnswer();
        verify(!demo.generating);
    }
    function test_questionRequestObjectionPersistInRecord() {
        demo.streamInterval = 100000;
        demo.beginInterview();
        completeSuggestion();
        compare(demo.triggerKind, "question");
        demo.queueQuestion(2);
        completeSuggestion();
        compare(demo.triggerKind, "request");
        demo.queueQuestion(3);
        completeSuggestion();
        compare(demo.triggerKind, "objection");
        demo.finishInterview();
        compare(demo.selectedRecord.questions.length, 3);
        compare(demo.selectedRecord.questions.map(function (item) {
            return item.kind;
        }).join(","), "question,request,objection");
        verify(demo.selectedRecord.questions.every(function (item) {
            return item.question.length > 0 && item.answer.length > 0;
        }));
    }
    function test_statementsOnlyTranscribeDuringPendingStreamingAndComplete() {
        demo.streamInterval = 100000;
        demo.beginInterview();
        const pending = demo.pendingQuestion;
        const waitingTicks = demo.pendingTicks;
        demo.queueQuestion(1);
        compare(demo.messages.length, 2);
        compare(demo.pendingQuestion, pending);
        compare(demo.pendingTicks, waitingTicks);
        compare(demo.currentQuestions.length, 0);
        while (demo.pendingTicks > 0)
            demo.advanceAnswer();
        demo.advanceAnswer();
        const activeQuestion = demo.question;
        const partialAnswer = demo.answer;
        const cursor = demo.streamCursor;
        demo.queueQuestion(4);
        compare(demo.messages.length, 3);
        compare(demo.question, activeQuestion);
        compare(demo.triggerKind, "question");
        compare(demo.answer, partialAnswer);
        compare(demo.streamCursor, cursor);
        compare(demo.pendingQuestion, "");
        compare(demo.currentQuestions.length, 1);
        completeSuggestion();
        const fullAnswer = demo.answer;
        demo.queueQuestion(1);
        verify(!demo.generating);
        compare(demo.answer, fullAnswer);
        compare(demo.triggerKind, "question");
        demo.finishInterview();
        compare(demo.selectedRecord.questions.length, 1);
        compare(demo.selectedRecord.transcript.length, 4);
    }
    function test_preflightUpdatesEngineState() {
        demo.engineStatus = "stopped";
        demo.startChecks();
        tryCompare(demo, "interviewActive", true);
        compare(demo.engineStatus, "ready");
        demo.finishInterview();
        demo.startScenario = "engine";
        demo.startChecks();
        tryCompare(demo, "checkState", "failed");
        compare(demo.engineStatus, "error");
    }
}
