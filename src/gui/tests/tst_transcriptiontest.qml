import QtQuick
import QtTest
import "../mock" as Mock

TestCase {
    id: testCase
    name: "TranscriptionTestLifecycle"
    when: windowShown

    Component {
        id: factory
        Mock.TranscriptionTestController {
            interval: 100000
        }
    }
    Component {
        id: demoFactory
        Mock.DemoController {
            checkInterval: 100000
            streamInterval: 100000
        }
    }
    Component {
        id: spyFactory
        SignalSpy {}
    }
    property var probe

    function init() {
        failOnWarning(/Binding loop detected/);
        probe = createTemporaryObject(factory, testCase);
        verify(probe !== null);
    }
    function cleanup() {
        probe.close();
    }
    function advance(count) {
        for (let i = 0; i < count; ++i)
            probe.advanceTick();
    }

    function test_openReadyStreamsBothChannelsWithIndependentSilence() {
        probe.open();
        compare(probe.status, "running");
        compare(probe.spkState, "waiting");
        compare(probe.micState, "waiting");
        advance(4);
        compare(probe.spkState, "receiving");
        compare(probe.micState, "waiting");
        verify(probe.spkLevel > 0 && probe.micLevel === 0);
        verify(probe.lines[0].text.length > 0 && !probe.lines[0].final);
        advance(9);
        compare(probe.spkState, "waiting");
        compare(probe.micState, "waiting");
        verify(probe.lines[0].final);
        advance(19);
        compare(probe.lines.length, 2);
        compare(probe.lines[0].source, "spk");
        compare(probe.lines[1].source, "mic");
        verify(probe.lines[0].time !== probe.lines[1].time);
        compare(probe.elapsedText, "00:08");
    }
    function test_engineStartupOnlyRequestedOnceAndStopCannotResume() {
        probe.engineStatus = "stopped";
        const spy = createTemporaryObject(spyFactory, testCase, {
            target: probe,
            signalName: "startEngineRequested"
        });
        probe.open();
        compare(probe.status, "starting");
        compare(spy.count, 1);
        probe.engineStatus = "starting";
        probe.open();
        compare(spy.count, 1);
        probe.stop();
        probe.engineStatus = "ready";
        advance(10);
        compare(probe.status, "stopped");
        compare(probe.lines.length, 0);
        verify(!probe.captureTimer.running);
        probe.restart();
        compare(probe.status, "running");
        compare(spy.count, 1);
    }
    function test_closeDuringStartupDiscardsTextAndCannotReopen() {
        probe.engineStatus = "starting";
        const spy = createTemporaryObject(spyFactory, testCase, {
            target: probe,
            signalName: "startEngineRequested"
        });
        probe.open();
        compare(probe.status, "starting");
        compare(spy.count, 0);
        probe.close();
        probe.engineStatus = "ready";
        advance(20);
        verify(!probe.opened);
        compare(probe.status, "idle");
        compare(probe.lines.length, 0);
        compare(probe.elapsedText, "00:00");
        verify(!probe.captureTimer.running);
        probe.open();
        compare(probe.status, "running");
        advance(4);
        verify(probe.lines.length > 0);
    }
    function test_stopFreezesAndRestartClearsPreviousRun() {
        probe.open();
        advance(24);
        probe.stop();
        const lines = JSON.stringify(probe.lines);
        const elapsed = probe.elapsedText;
        advance(32);
        compare(JSON.stringify(probe.lines), lines);
        compare(probe.elapsedText, elapsed);
        compare(probe.spkLevel, 0);
        compare(probe.micLevel, 0);
        compare(probe.spkState, "stopped");
        compare(probe.micState, "stopped");
        probe.restart();
        compare(probe.lines.length, 0);
        compare(probe.elapsedText, "00:00");
        advance(4);
        compare(probe.lines[0].id, 1);
        probe.close();
        compare(probe.lines.length, 0);
    }
    function test_channelFailureDoesNotStopOtherChannel() {
        probe.spkAvailable = false;
        probe.open();
        compare(probe.spkState, "error");
        compare(probe.micState, "waiting");
        advance(32);
        compare(probe.status, "running");
        compare(probe.lines.length, 1);
        compare(probe.lines[0].source, "mic");
        verify(probe.lines[0].final);
        compare(probe.spkLevel, 0);
        probe.spkAvailable = true;
        advance(4);
        compare(probe.spkState, "receiving");
        compare(probe.lines[1].source, "spk");
        verify(!probe.lines[1].final);
        probe.spkAvailable = false;
        compare(probe.spkState, "error");
        verify(probe.lines[1].final);
        compare(probe.spkLevel, 0);
    }
    function test_engineFailureFreezesAndExplicitRestartRecovers() {
        probe.open();
        advance(4);
        probe.engineStatus = "error";
        const lines = JSON.stringify(probe.lines);
        compare(probe.status, "error");
        compare(probe.spkState, "error");
        compare(probe.micState, "error");
        verify(probe.errorMessage.length > 0);
        advance(32);
        compare(JSON.stringify(probe.lines), lines);
        probe.engineStatus = "ready";
        compare(probe.status, "error");
        probe.restart();
        compare(probe.status, "running");
        compare(probe.errorMessage, "");
        compare(probe.lines.length, 0);
    }
    function test_longRunBoundsTranscriptMemory() {
        probe.open();
        advance(16 * 65);
        compare(probe.lines.length, probe.maximumLines);
        verify(probe.lines[0].id > 1);
        verify(probe.lines.every(function (line) {
            return line.text.length > 0 && line.final;
        }));
        verify(probe.lines.every(function (line, index, list) {
            return index === 0 || line.id > list[index - 1].id;
        }));
    }
    function test_sharedEngineAndFormalConversationStayIsolated() {
        const demo = createTemporaryObject(demoFactory, testCase);
        demo.route = "audio";
        demo.systemDevice = "耳机输出";
        demo.microphone = "外接麦克风";
        demo.transcriptionTest.interval = 100000;
        const history = JSON.stringify(demo.records);
        demo.backgroundText = "保存的背景";
        demo.transcriptionTest.open();
        compare(demo.transcriptionTest.spkDevice, "耳机输出");
        compare(demo.transcriptionTest.micDevice, "外接麦克风");
        for (let i = 0; i < 32; ++i)
            demo.transcriptionTest.advanceTick();
        compare(JSON.stringify(demo.records), history);
        compare(demo.messages.length, 0);
        compare(demo.currentQuestions.length, 0);
        compare(demo.backgroundText, "保存的背景");
        demo.route = "home";
        verify(!demo.transcriptionTest.opened);
        compare(demo.transcriptionTest.lines.length, 0);
        demo.transcriptionTest.open();
        verify(!demo.transcriptionTest.opened);
        demo.route = "audio";
        demo.transcriptionTest.open();
        demo.startChecks();
        verify(!demo.transcriptionTest.opened);
        demo.transcriptionTest.open();
        verify(!demo.transcriptionTest.opened);
        demo.cancelChecks();
        demo.transcriptionTest.open();
        demo.beginInterview();
        verify(!demo.transcriptionTest.opened);
        demo.transcriptionTest.open();
        verify(!demo.transcriptionTest.opened);
        demo.finishInterview();
    }
    function test_sharedEngineStartupMayFinishAfterTestClosed() {
        const demo = createTemporaryObject(demoFactory, testCase);
        demo.route = "audio";
        demo.engineStatus = "stopped";
        demo.engineTimer.interval = 20;
        demo.transcriptionTest.open();
        compare(demo.engineStatus, "starting");
        compare(demo.transcriptionTest.status, "starting");
        demo.transcriptionTest.close();
        tryCompare(demo, "engineStatus", "ready");
        verify(!demo.transcriptionTest.opened);
        compare(demo.transcriptionTest.status, "idle");
        compare(demo.transcriptionTest.lines.length, 0);
        // A later external failure cannot be overwritten by a stale startup callback.
        demo.engineStatus = "stopped";
        demo.startEngine();
        demo.engineStatus = "error";
        wait(50);
        compare(demo.engineStatus, "error");
    }
}
