pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TextBlock
// Keep application C++ types discoverable in Qt Creator's classic code model.
import TextBlockLab

Window {
    id: window
    property string fontLicenseText: ""

    TextClipboard {
        id: clipboard
    }
    component CopyActions: TextCopyActions {
        id: copyActions
        anchors.fill: parent
        onCopyRequested: function (text) {
            if (clipboard.copyText(text))
                copyActions.showCopied();
        }
    }

    width: 1120
    height: 900
    minimumWidth: 760
    minimumHeight: 640
    visible: true
    title: qsTr("春风 · TextBlock 实验室")
    color: "#f4f5f1"

    Dialog {
        id: fontLicenseDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(window.width - 56, 800)
        height: Math.min(window.height - 56, 640)
        modal: true
        title: qsTr("字体许可")
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent
            TextArea {
                text: window.fontLicenseText
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.pixelSize: 12
            }
        }
    }

    readonly property color ink: "#24352d"
    readonly property color muted: "#6d7b73"
    readonly property color accent: "#35634a"
    readonly property color lineColor: "#e2e7df"
    readonly property font paragraphFont: Qt.font({
        pixelSize: 18
    })
    readonly property real requestedMaximumWidth: Math.round(widthSlider.value)
    readonly property real comparisonMaximumWidth: Math.min(requestedMaximumWidth, (comparisonGrid.width - comparisonGrid.columnSpacing * (comparisonGrid.columns - 1)) / comparisonGrid.columns - 40)

    // Every character in this stream is a single BMP code point with no combining marks.
    // Slicing these snapshots cannot split a grapheme or surrogate pair.
    readonly property string streamSource: "春风拂过，答案慢慢展开。一个好的回答，往往从理解问题开始：先说明背景，再梳理关键选择，最后给出清晰的结论。这里的文字由本地计时器逐步追加，每次都提交一份完整的内容快照。拖动上方的宽度滑块，可以观察短句如何贴合内容、长段落如何自然换行。TextBlock 把一个段落放进一个气泡；内容持续增长时，其他段落仍然保持各自的布局。你也可以随时暂停，调整窗口，再继续观察。"
    property int streamLength: 39
    property bool streaming: false
    readonly property var streamContent: ({
            text: streamSource.slice(0, streamLength),
            ranges: []
        })
    readonly property string streamStatus: streaming ? qsTr("生成中") : streamLength === streamSource.length ? qsTr("已完成") : streamLength === 0 ? qsTr("待开始") : qsTr("已暂停")

    property bool observationEnabled: true
    property int referenceLayoutCallbacks: 0
    property int referenceCallbacksBase: 0
    property int customLayoutBase: 0
    property int commonInputUpdates: 0
    property int commonInputBase: 0
    property bool inputCountingReady: false

    onStreamContentChanged: {
        if (inputCountingReady && !benchmarkRunning)
            commonInputUpdates++;
    }
    onComparisonMaximumWidthChanged: {
        if (inputCountingReady && !benchmarkRunning)
            commonInputUpdates++;
    }
    Component.onCompleted: inputCountingReady = true

    function resetObservation(): void {
        customLayoutBase = streamBlock.layoutCount;
        referenceCallbacksBase = referenceLayoutCallbacks;
        commonInputBase = commonInputUpdates;
    }

    Connections {
        target: window.observationEnabled && !window.benchmarkRunning ? referenceText : null
        function onLineLaidOut(line) {
            if (line.number === 0)
                window.referenceLayoutCallbacks++;
        }
    }

    readonly property int benchmarkWarmupBatches: 5
    readonly property int benchmarkFormalBatches: 30
    property bool benchmarkRunning: false
    property bool benchmarkAwaiting: false
    property int benchmarkGeneration: 0
    property int benchmarkRenderer: 0
    property int benchmarkStep: 0
    property int benchmarkInstanceCount: 6
    property var benchmarkInputs: []
    property var benchmarkSnapshot: ({
            text: "相同的初始内容。Ready for the same workload.",
            ranges: []
        })
    property real benchmarkWidth: 360
    property var benchmarkResults: [[], []]
    property string benchmarkStatus: qsTr("尚未运行")
    readonly property string benchmarkRendererName: benchmarkRenderer === 0 ? "TextBlock" : "Qt Quick Text"
    readonly property string benchmarkProgress: benchmarkStep < benchmarkWarmupBatches ? qsTr("预热 %1 / %2").arg(benchmarkStep).arg(benchmarkWarmupBatches) : qsTr("正式批次 %1 / %2").arg(benchmarkStep - benchmarkWarmupBatches).arg(benchmarkFormalBatches)

    // Each entry is created before timing starts. Text contains only safe BMP
    // characters, and both renderers consume these exact snapshots in order.
    function prepareBenchmarkInputs(): var {
        const result = [];
        const widths = [260, 420, 320, 480, 280, 400];
        for (let i = 0; i < benchmarkWarmupBatches + benchmarkFormalBatches; ++i) {
            result.push({
                content: {
                    text: streamSource.slice(0, 48 + (i % 7) * 8),
                    ranges: []
                },
                maximumWidth: widths[i % widths.length]
            });
        }
        return result;
    }

    function resetBenchmarkInput(): void {
        benchmarkSnapshot = {
            text: "相同的初始内容。Ready for the same workload.",
            ranges: []
        };
        benchmarkWidth = 360;
        benchmarkStep = 0;
    }

    function startBenchmark(): void {
        if (benchmarkRunning)
            return;
        benchmarkGeneration++;
        streaming = false;
        benchmarkInstanceCount = Number(benchmarkCountPicker.currentText);
        benchmarkInputs = prepareBenchmarkInputs();
        benchmarkResults = [[], []];
        benchmarkRenderer = 0;
        benchmarkAwaiting = false;
        benchmarkStatus = qsTr("运行中");
        resetBenchmarkInput();
        benchmarkRunning = true;
    }

    function finishBenchmark(message: string): void {
        benchmarkGeneration++;
        benchmarkRunning = false;
        benchmarkAwaiting = false;
        experimentMetrics.cancel();
        // Publish the accumulated results once: result-table bindings do not
        // add per-batch work to the measured run.
        benchmarkResults = [benchmarkResults[0].slice(), benchmarkResults[1].slice()];
        benchmarkStatus = message;
        resetObservation();
    }

    function scheduleNextBenchmarkBatch(): void {
        const generation = benchmarkGeneration;
        Qt.callLater(function () {
            if (generation === window.benchmarkGeneration)
                window.runNextBenchmarkBatch();
        });
    }

    function runNextBenchmarkBatch(): void {
        if (!benchmarkRunning || benchmarkAwaiting || benchmarkLoader.status !== Loader.Ready)
            return;
        if (benchmarkStep === benchmarkWarmupBatches + benchmarkFormalBatches) {
            if (benchmarkRenderer === 0) {
                resetBenchmarkInput();
                benchmarkRenderer = 1;
                // Changing the source component destroys the previous renderer.
                // The new Loader.onLoaded schedules its first warm-up batch.
            } else {
                finishBenchmark(qsTr("已完成"));
            }
            return;
        }
        const input = benchmarkInputs[benchmarkStep];
        benchmarkAwaiting = true;
        experimentMetrics.measureBatch(benchmarkLoader.item, function () {
            window.benchmarkSnapshot = input.content;
            window.benchmarkWidth = input.maximumWidth;
        });
    }

    function validBenchmarkSamples(renderer: int): var {
        return benchmarkResults[renderer].filter(function (sample) {
            return sample.settled && Number.isFinite(sample.gui) && Number.isFinite(sample.submission) && Number.isFinite(sample.frame) && sample.gui >= 0 && sample.submission >= 0 && sample.frame >= 0;
        });
    }

    function metricSummary(renderer: int, key: string, decimals: int): string {
        const samples = validBenchmarkSamples(renderer);
        if (samples.length === 0)
            return "—";
        const values = samples.map(function (sample) {
            return sample[key];
        });
        values.sort(function (a, b) {
            return a - b;
        });
        const mid = Math.floor(values.length / 2);
        const median = values.length % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
        const p95 = values[Math.ceil(values.length * 0.95) - 1];
        return median.toFixed(decimals) + " / " + p95.toFixed(decimals);
    }

    function sampleCount(renderer: int): string {
        return qsTr("%1 / %2").arg(validBenchmarkSamples(renderer).length).arg(benchmarkResults[renderer].length);
    }

    ExperimentMetrics {
        id: experimentMetrics
        onBatchMeasured: function (guiMicroseconds, submissionMilliseconds, framePreparationMilliseconds, settled) {
            if (!window.benchmarkRunning || !window.benchmarkAwaiting)
                return;
            if (window.benchmarkStep >= window.benchmarkWarmupBatches) {
                window.benchmarkResults[window.benchmarkRenderer].push({
                    gui: guiMicroseconds,
                    submission: submissionMilliseconds,
                    frame: framePreparationMilliseconds,
                    settled: settled
                });
            }
            window.benchmarkAwaiting = false;
            window.benchmarkStep++;
            window.scheduleNextBenchmarkBatch();
        }
        onFailed: function (message) {
            if (window.benchmarkRunning)
                window.finishBenchmark(qsTr("已停止：%1").arg(message));
        }
    }

    Component {
        id: benchmarkCustomComponent
        Item {
            Repeater {
                model: window.benchmarkInstanceCount
                delegate: TextBlock {
                    required property int index
                    x: 24 + (index % 10) * 8
                    y: 16 + Math.floor(index / 10) * 6
                    content: window.benchmarkSnapshot
                    maximumWidth: window.benchmarkWidth
                    padding: 16
                    font: window.paragraphFont
                    textColor: window.ink
                    background: Rectangle {
                        color: "#000"
                        radius: 12
                    }
                }
            }
        }
    }

    Component {
        id: benchmarkNativeComponent
        Item {
            Repeater {
                model: window.benchmarkInstanceCount
                delegate: Rectangle {
                    id: benchmarkNativeBubble
                    required property int index
                    x: 24 + (index % 10) * 8
                    y: 16 + Math.floor(index / 10) * 6
                    width: Math.min(window.benchmarkWidth, benchmarkNativeText.implicitWidth + 32)
                    height: benchmarkNativeText.height + 32
                    radius: 12
                    color: "#f2f3f0"
                    Text {
                        id: benchmarkNativeText
                        x: 16
                        y: 16
                        width: Math.max(0, benchmarkNativeBubble.width - 32)
                        text: window.benchmarkSnapshot.text
                        font: window.paragraphFont
                        color: window.ink
                        textFormat: Text.PlainText
                        renderType: Text.QtRendering
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    function styledContent(text: string, boldText: string, highlightText: string): var {
        const ranges = [];
        const boldStart = text.indexOf(boldText);
        const highlightStart = text.indexOf(highlightText);
        if (boldText.length > 0 && boldStart >= 0)
            ranges.push({
                start: boldStart,
                end: boldStart + boldText.length,
                bold: true
            });
        if (highlightText.length > 0 && highlightStart >= 0)
            ranges.push({
                start: highlightStart,
                end: highlightStart + highlightText.length,
                highlight: true
            });
        return {
            text: text,
            ranges: ranges
        };
    }

    readonly property var samples: [
        {
            title: qsTr("粗体 × 高亮"),
            note: qsTr("两个区间独立传入，交叠部分同时生效。"),
            content: styledContent("先理解问题，再给出清晰的结论。让重点自然浮现。", "清晰的结论", "给出清晰")
        },
        {
            title: qsTr("中文 · English · Emoji"),
            note: qsTr("观察不同字体回退、组合字符与表情的边缘。"),
            content: styledContent("你好，世界。Hello, Qt Quick! 👋🏽 一起写下新的想法 👨‍👩‍👧‍👦，也读一读 café 与 café。", "Qt Quick", "👨‍👩‍👧‍👦")
        },
        {
            title: qsTr("长词与边界"),
            note: qsTr("连续长词超过可用宽度时，也应保持可读。"),
            content: styledContent("一次连续文本测试：Supercalifragilisticexpialidocious_0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZ。随后回到中文。", "连续文本测试", "0123456789")
        }
    ]

    readonly property var repeatedSamples: [styledContent("短句，也有自己的空间。", "短句", ""), styledContent("每个气泡只承载一个段落。文字变长时，宽度先增长到上限，再自然换行。", "一个段落", "自然换行"), styledContent("Build small, observe carefully. 中文与 English 可以在同一个段落中自然相遇。", "observe carefully", "自然相遇"), styledContent("一个重要的结论，既可以加粗，也可以高亮。", "重要的结论", "结论，既可以"), styledContent("今天也有新发现 🌱 Keep going!", "Keep going!", "🌱"), styledContent("宽度调整影响每个实例；流式追加只更新上方的演示内容。可以停下来看看各自的布局次数。", "每个实例", "布局次数")]

    Timer {
        interval: 45
        repeat: true
        running: window.streaming
        onTriggered: {
            window.streamLength = Math.min(window.streamLength + 2, window.streamSource.length);
            if (window.streamLength === window.streamSource.length)
                window.streaming = false;
        }
    }

    component SmallText: Text {
        color: window.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }

    component SectionTitle: Text {
        color: window.ink
        font.pixelSize: 16
        font.weight: Font.DemiBold
        textFormat: Text.PlainText
    }

    component Card: Rectangle {
        default property alias contents: cardBody.data
        implicitHeight: cardBody.implicitHeight + 40
        color: "#ffffff"
        radius: 16
        border.color: window.lineColor
        ColumnLayout {
            id: cardBody
            x: 20
            y: 20
            width: parent.width - 40
            spacing: 14
        }
    }

    component ActionButton: AppActionButton {
        foregroundColor: window.ink
        accentColor: window.accent
    }

    Flickable {
        id: scroller
        visible: !window.benchmarkRunning
        anchors.fill: parent
        anchors.margins: 28
        contentWidth: width
        contentHeight: page.implicitHeight + 16
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: page
            width: scroller.width - 12
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                ColumnLayout {
                    spacing: 5
                    Text {
                        text: qsTr("春风 / INTERFACE LAB")
                        color: window.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.3
                    }
                    Text {
                        text: qsTr("让文字，自然展开。")
                        color: window.ink
                        font.pixelSize: 30
                        font.weight: Font.DemiBold
                    }
                    SmallText {
                        text: qsTr("TextBlock  ·  单段落文本气泡实验")
                        font.pixelSize: 13
                    }
                    SmallText {
                        text: qsTr("中文：OPPO Sans 4.0  ·  English / 0123：JetBrains Mono")
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    implicitWidth: 100
                    implicitHeight: 28
                    radius: 14
                    color: "#e7eee2"
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("● 本地实验")
                        color: window.accent
                        font.pixelSize: 12
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 18
                    ColumnLayout {
                        spacing: 4
                        SectionTitle {
                            text: qsTr("气泡宽度")
                        }
                        SmallText {
                            text: qsTr("上限包含内边距")
                        }
                    }
                    Slider {
                        id: widthSlider
                        Layout.fillWidth: true
                        Layout.minimumWidth: 180
                        from: 240
                        to: 620
                        value: 440
                        stepSize: 1
                        Accessible.name: qsTr("气泡最大宽度")
                    }
                    Text {
                        Layout.preferredWidth: 68
                        text: window.requestedMaximumWidth + " px"
                        color: window.accent
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle {
                        text: qsTr("01 / 流式内容与原生对照")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    SmallText {
                        text: qsTr("有效宽度 %1 px · 字号 18 px").arg(Math.round(window.comparisonMaximumWidth))
                    }
                }
                GridLayout {
                    id: comparisonGrid
                    Layout.fillWidth: true
                    columns: page.width >= 880 ? 2 : 1
                    columnSpacing: 16
                    rowSpacing: 16
                    uniformCellWidths: true

                    Card {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                text: "TextBlock"
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SmallText {
                                text: window.streamStatus
                                color: window.accent
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: Math.max(130, streamBlock.height)
                            TextBlock {
                                id: streamBlock
                                content: window.streamContent
                                maximumWidth: window.comparisonMaximumWidth
                                padding: 16
                                font: window.paragraphFont
                                textColor: window.ink
                                background: Rectangle {
                                    color: lineColor
                                    radius: 12
                                }
                                highlightColor: "#e2eeab"
                                CopyActions {
                                    id: streamActions
                                    text: String(streamBlock.content.text ?? "")
                                }
                            }
                            SmallText {
                                visible: window.streamLength === 0
                                x: 16
                                y: 16
                                text: qsTr("点击开始，让文字慢慢展开。")
                            }
                        }
                        SmallText {
                            Layout.fillWidth: true
                            text: window.observationEnabled ? qsTr("布局重建次数 %1  ·  最近布局 %2 μs  ·  %3 行").arg(streamBlock.layoutCount - window.customLayoutBase).arg(streamBlock.layoutMicroseconds.toFixed(1)).arg(streamBlock.lineCount) : qsTr("观察关闭  ·  %1 行").arg(streamBlock.lineCount)
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                text: "Qt Quick Text"
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SmallText {
                                text: "PlainText"
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: Math.max(130, referenceBubble.height)
                            Rectangle {
                                id: referenceBubble
                                width: Math.min(window.comparisonMaximumWidth, referenceText.implicitWidth + 32)
                                height: referenceText.height + 32
                                color: "#f2f3f0"
                                radius: 12
                                Text {
                                    id: referenceText
                                    x: 16
                                    y: 16
                                    width: Math.max(0, referenceBubble.width - 32)
                                    text: window.streamContent.text
                                    font: window.paragraphFont
                                    color: window.ink
                                    textFormat: Text.PlainText
                                    renderType: Text.QtRendering
                                    wrapMode: Text.Wrap
                                }
                            }
                            SmallText {
                                visible: window.streamLength === 0
                                x: 16
                                y: 16
                                text: qsTr("同一份文本，等待开始。")
                            }
                        }
                        SmallText {
                            Layout.fillWidth: true
                            text: window.observationEnabled ? qsTr("首行布局回调次数（观测） %1  ·  %2 行").arg(window.referenceLayoutCallbacks - window.referenceCallbacksBase).arg(referenceText.lineCount) : qsTr("观察关闭  ·  %1 行").arg(referenceText.lineCount)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ActionButton {
                        primary: true
                        text: window.streaming ? qsTr("暂停") : window.streamLength === window.streamSource.length ? qsTr("再播一次") : qsTr("开始 / 继续")
                        onClicked: {
                            if (window.streamLength === window.streamSource.length)
                                window.streamLength = 0;
                            window.streaming = !window.streaming;
                        }
                    }
                    ActionButton {
                        text: qsTr("重置")
                        onClicked: {
                            window.streaming = false;
                            window.streamLength = 0;
                        }
                    }
                    ActionButton {
                        text: qsTr("复制全文")
                        onClicked: streamActions.copy()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    SmallText {
                        text: qsTr("%1 / %2 字符").arg(window.streamLength).arg(window.streamSource.length)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Switch {
                        text: qsTr("布局观察")
                        checked: window.observationEnabled
                        onToggled: {
                            window.observationEnabled = checked;
                            if (checked)
                                window.resetObservation();
                        }
                    }
                    ActionButton {
                        text: qsTr("重置统计")
                        onClicked: window.resetObservation()
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    SmallText {
                        text: qsTr("共同输入更新 %1 次").arg(window.commonInputUpdates - window.commonInputBase)
                    }
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("两种计数口径不同：左侧记录布局重建，右侧记录原生 Text 首行布局回调。共同输入按文本或有效宽度变化计数；重开观察会重置统计起点。右键气泡可复制全文。")
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("最近布局耗时仅覆盖 TextBlock 的文本布局，不包含解析、场景图提交或绘制。性能比较使用下方独立测试。")
                }
            }

            Card {
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle {
                        text: qsTr("02 / 同负载性能测试")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: benchmarkCountPicker
                        model: [6, 30, 100]
                        implicitWidth: 108
                        displayText: qsTr("%1 个实例").arg(currentText)
                        Accessible.name: qsTr("性能测试实例数")
                    }
                    ActionButton {
                        primary: true
                        text: qsTr("开始测试")
                        onClicked: window.startBenchmark()
                    }
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("先 TextBlock，再 Qt Quick Text；各预热 5 批、测量 30 批。使用同一份预生成文本和宽度序列；运行时关闭布局观察与流式更新。")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: 10
                    columnSpacing: 14
                    SmallText {
                        Layout.fillWidth: true
                        text: qsTr("中位数 / P95")
                    }
                    SmallText {
                        Layout.preferredWidth: 160
                        text: "TextBlock"
                        color: window.ink
                    }
                    SmallText {
                        Layout.preferredWidth: 160
                        text: "Qt Quick Text"
                        color: window.ink
                    }
                    SmallText {
                        Layout.fillWidth: true
                        text: qsTr("属性更新 + polish（μs）")
                    }
                    SmallText {
                        text: window.metricSummary(0, "gui", 1)
                    }
                    SmallText {
                        text: window.metricSummary(1, "gui", 1)
                    }
                    SmallText {
                        Layout.fillWidth: true
                        text: qsTr("更新至帧提交（ms）")
                    }
                    SmallText {
                        text: window.metricSummary(0, "submission", 3)
                    }
                    SmallText {
                        text: window.metricSummary(1, "submission", 3)
                    }
                    SmallText {
                        Layout.fillWidth: true
                        text: qsTr("帧同步至渲染记录（ms）")
                    }
                    SmallText {
                        text: window.metricSummary(0, "frame", 3)
                    }
                    SmallText {
                        text: window.metricSummary(1, "frame", 3)
                    }
                    SmallText {
                        Layout.fillWidth: true
                        text: qsTr("有效样本 / 已完成正式批次")
                    }
                    SmallText {
                        text: window.sampleCount(0)
                    }
                    SmallText {
                        text: window.sampleCount(1)
                    }
                }
                SmallText {
                    Layout.fillWidth: true
                    color: window.accent
                    text: window.benchmarkStatus + (window.benchmarkResults[0].length + window.benchmarkResults[1].length > 0 ? qsTr("  ·  本轮固定 %1 个实例").arg(window.benchmarkInstanceCount) : "")
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("固定 640 × 320 视口内交叠摆放全部实例，只加载当前一种组件。属性更新 + polish 含整棵样本树的稳定检查开销；帧提交含调度等待，不代表 GPU 完成或屏幕呈现；帧准备时间是整个窗口同步至渲染记录结束的墙钟区间。")
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("最多 8 次 polish 遍历后尺寸仍变化的批次标记为未稳定，并与无效样本一起排除统计；这不代表 Qt 内部布局轮数。停止会保留已完成批次。")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                SectionTitle {
                    text: qsTr("03 / 样式与字符边界")
                }
                Repeater {
                    model: window.samples
                    delegate: Card {
                        id: sampleCard
                        required property var modelData
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            SectionTitle {
                                text: sampleCard.modelData.title
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            SmallText {
                                text: qsTr("%1 行").arg(sampleBlock.lineCount)
                            }
                        }
                        TextBlock {
                            id: sampleBlock
                            content: sampleCard.modelData.content
                            maximumWidth: Math.min(window.requestedMaximumWidth, sampleCard.width - 40)
                            padding: 16
                            font: window.paragraphFont
                            textColor: window.ink
                            background: Rectangle {
                                color: "#f2f5ee"
                                radius: 12
                            }
                            highlightColor: "#e2eeab"
                            CopyActions {
                                text: String(sampleBlock.content.text ?? "")
                            }
                        }
                        SmallText {
                            Layout.fillWidth: true
                            text: sampleCard.modelData.note
                        }
                    }
                }
            }

            Card {
                id: multipleParagraphsCard
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    SectionTitle {
                        text: qsTr("04 / 多个段落，各自布局")
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ComboBox {
                        id: countPicker
                        model: [6, 30, 100]
                        implicitWidth: 108
                        displayText: qsTr("%1 个实例").arg(currentText)
                        Accessible.name: qsTr("多段落实例数")
                    }
                }
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("默认 6 个独立实例。切换到 30 / 100 个后，可观察宽度变化与流式更新的影响。")
                }
                ColumnLayout {
                    id: manyBlocks
                    Layout.fillWidth: true
                    spacing: 16
                    Repeater {
                        model: Number(countPicker.currentText)
                        delegate: ColumnLayout {
                            id: repeatedBlock
                            required property int index
                            Layout.fillWidth: true
                            spacing: 6
                            TextBlock {
                                id: block
                                content: window.repeatedSamples[repeatedBlock.index % window.repeatedSamples.length]
                                // Use the available card width, not a nested layout's
                                // result that also depends on this paragraph's size.
                                maximumWidth: Math.min(window.requestedMaximumWidth, multipleParagraphsCard.width - 40)
                                padding: 16
                                font: window.paragraphFont
                                textColor: window.ink
                                background: Rectangle {
                                    color: repeatedBlock.index % 2 === 0 ? "#edf3e8" : "#f2f3f0"
                                    radius: 12
                                }
                                highlightColor: "#e2eeab"
                                CopyActions {
                                    text: String(block.content.text ?? "")
                                }
                            }
                            SmallText {
                                text: qsTr("#%1  ·  布局重建 %2 次  ·  %3 μs").arg(repeatedBlock.index + 1).arg(block.layoutCount).arg(block.layoutMicroseconds.toFixed(1))
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                SmallText {
                    Layout.fillWidth: true
                    text: qsTr("春风 · TextBlock 实验室    /    本地静态样本，无服务连接。")
                }
                ActionButton {
                    text: qsTr("字体许可")
                    onClicked: fontLicenseDialog.open()
                }
            }
        }
    }
    Item {
        anchors.fill: parent
        anchors.margins: 28
        visible: window.benchmarkRunning

        ColumnLayout {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 16
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    spacing: 5
                    SectionTitle {
                        text: qsTr("同负载性能测试 · %1").arg(window.benchmarkRendererName)
                    }
                    SmallText {
                        text: qsTr("%1 个实例  ·  %2  ·  观察关闭").arg(window.benchmarkInstanceCount).arg(window.benchmarkProgress)
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: qsTr("停止并保留结果")
                    onClicked: window.finishBenchmark(qsTr("已停止，保留部分结果"))
                }
            }
            Rectangle {
                id: benchmarkViewport
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 640
                Layout.preferredHeight: 320
                color: "white"
                clip: true
                Loader {
                    id: benchmarkLoader
                    anchors.fill: parent
                    active: window.benchmarkRunning
                    sourceComponent: window.benchmarkRenderer === 0 ? benchmarkCustomComponent : benchmarkNativeComponent
                    onLoaded: window.scheduleNextBenchmarkBatch()
                }
            }
            SmallText {
                Layout.fillWidth: true
                text: qsTr("全部实例位于固定视口内并交叠显示。每批同时更新文本快照和宽度；保持窗口大小不变可减少外部干扰。")
            }
            SmallText {
                Layout.fillWidth: true
                text: qsTr("测量：属性更新 + polish / 更新至帧提交 / 整个窗口的帧同步至渲染记录。结果将在两组完成或停止后显示。")
            }
        }
    }
}
