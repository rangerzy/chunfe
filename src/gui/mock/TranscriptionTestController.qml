import QtQuick

// Local, explicitly scripted samples. This does not capture or recognize audio.
QtObject {
    id: root

    property bool opened: false
    property bool allowTesting: true
    property string status: "idle"
    property string engineStatus: "ready"
    property string spkDevice: "系统默认输出"
    property string micDevice: "系统默认麦克风"
    property bool spkAvailable: true
    property bool micAvailable: true
    property real spkLevel: 0
    property real micLevel: 0
    readonly property string spkState: channelState(spkAvailable, spkLevel)
    readonly property string micState: channelState(micAvailable, micLevel)
    property var lines: []
    property string errorMessage: ""
    property int interval: 250
    property int elapsedMilliseconds: 0
    readonly property string elapsedText: formatTime(Math.floor(elapsedMilliseconds / 1000))
    readonly property int maximumLines: 40

    property int tick: 0
    property int currentSlot: -1
    property int lineSequence: 0
    property int currentLineId: -1
    readonly property var samples: [
        {
            source: "spk",
            text: "我们先确认一下今天讨论的内容。"
        },
        {
            source: "mic",
            text: "好的，我这边可以清楚地听到。"
        },
        {
            source: "spk",
            text: "接下来一起核对时间和安排。"
        },
        {
            source: "mic",
            text: "我会把确认的事项整理下来。"
        }
    ]

    signal startEngineRequested

    function formatTime(seconds: int): string {
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0");
    }
    function channelState(available: bool, level: real): string {
        if (!opened || status === "idle" || status === "stopped")
            return "stopped";
        if (!available || status === "error")
            return "error";
        if (status === "starting")
            return "starting";
        return level > 0 ? "receiving" : "waiting";
    }
    function resetCapture() {
        lines = [];
        tick = 0;
        elapsedMilliseconds = 0;
        currentSlot = -1;
        currentLineId = -1;
        lineSequence = 0;
        spkLevel = 0;
        micLevel = 0;
        errorMessage = "";
    }
    function startCapture() {
        resetCapture();
        if (engineStatus === "ready") {
            status = "running";
        } else {
            status = "starting";
            if (engineStatus !== "starting")
                startEngineRequested();
        }
    }
    function open() {
        if (!allowTesting || opened)
            return;
        opened = true;
        startCapture();
    }
    function restart() {
        if (!allowTesting || !opened)
            return;
        startCapture();
    }
    function settlePartialLine() {
        if (!lines.length || lines[lines.length - 1].final)
            return;
        const next = lines.slice();
        next[next.length - 1] = Object.assign({}, next[next.length - 1], {
            final: true
        });
        lines = next;
    }
    function stop() {
        if (!opened)
            return;
        status = "stopped";
        spkLevel = 0;
        micLevel = 0;
        settlePartialLine();
    }
    function close() {
        // A later shared-engine startup may complete, but cannot reopen this test.
        opened = false;
        status = "idle";
        resetCapture();
    }
    function failEngine() {
        status = "error";
        spkLevel = 0;
        micLevel = 0;
        settlePartialLine();
        errorMessage = engineStatus === "error" ? qsTr("转写引擎启动失败，请重新测试。") : qsTr("转写引擎已停止，请重新测试。");
    }
    function advanceTick() {
        if (!opened || !allowTesting || status !== "running")
            return;
        tick++;
        // One mock step represents 250 ms; the timer interval is adjustable in tests.
        elapsedMilliseconds += 250;
        const slot = Math.floor((tick - 1) / 16);
        const phase = (tick - 1) % 16;
        const sample = samples[slot % samples.length];
        const available = sample.source === "spk" ? spkAvailable : micAvailable;
        if (slot !== currentSlot) {
            currentSlot = slot;
            currentLineId = -1;
        }
        const receiving = phase >= 3 && phase <= 10 && available;
        const level = receiving ? [0.18, 0.52, 0.79, 0.4, 0.64, 0.28, 0.11, 0.35][phase - 3] : 0;
        spkLevel = sample.source === "spk" ? level : 0;
        micLevel = sample.source === "mic" ? level : 0;
        if (!receiving)
            return;
        const count = Math.min(sample.text.length, (phase - 2) * 3);
        const text = sample.text.slice(0, count);
        if (currentLineId < 0) {
            currentLineId = ++lineSequence;
            lines = lines.concat([
                {
                    id: currentLineId,
                    source: sample.source,
                    time: elapsedText,
                    text: text,
                    final: count === sample.text.length
                }
            ]).slice(-maximumLines);
            return;
        }
        const next = lines.slice();
        const index = next.findIndex(function (line) {
            return line.id === root.currentLineId;
        });
        if (index >= 0 && next[index].text !== text) {
            next[index] = Object.assign({}, next[index], {
                text: text,
                final: count === sample.text.length
            });
            lines = next;
        }
    }

    onAllowTestingChanged: {
        if (!allowTesting)
            close();
    }
    onEngineStatusChanged: {
        if (!opened || (status !== "starting" && status !== "running"))
            return;
        if (engineStatus === "ready") {
            if (status === "starting")
                status = "running";
        } else if (engineStatus !== "starting" || status === "running") {
            failEngine();
        }
    }
    onSpkAvailableChanged: {
        if (!spkAvailable) {
            spkLevel = 0;
            if (lines.length && lines[lines.length - 1].source === "spk")
                settlePartialLine();
        }
    }
    onMicAvailableChanged: {
        if (!micAvailable) {
            micLevel = 0;
            if (lines.length && lines[lines.length - 1].source === "mic")
                settlePartialLine();
        }
    }

    property Timer captureTimer: Timer {
        interval: Math.max(16, root.interval)
        repeat: true
        running: root.opened && root.allowTesting && root.status === "running"
        onTriggered: root.advanceTick()
    }
}
