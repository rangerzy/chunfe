pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Chunfe.Ui

PageFrame {
    id: root
    required property var controller
    property var audioDevices: null
    property var audioLevelTest: null
    property var offlineTranscription: null
    property bool pendingTranscriptionStart: false
    property bool deviceSelectorsReady: false
    maximumContentWidth: 640
    readonly property string engineStatus: offlineTranscription ? offlineTranscription.engineStatus : controller.engineStatus
    readonly property string engineLabel: engineStatus === "ready" ? qsTr("转写就绪") : engineStatus === "starting" ? qsTr("正在启动") : engineStatus === "error" ? qsTr("启动失败") : qsTr("引擎未启动")
    Component.onCompleted: {
        deviceSelectorsReady = true
        syncDeviceSelection()
        if (audioDevices)
            audioDevices.refresh()
    }
    Component.onDestruction: {
        deviceSelectorsReady = false
        pendingTranscriptionStart = false
        if (audioLevelTest)
            audioLevelTest.stop()
    }
    function openTranscriptionWhenCaptureFree() {
        if (!pendingTranscriptionStart || !offlineTranscription || !audioLevelTest || audioLevelTest.active || audioLevelTest.stopping)
            return
        pendingTranscriptionStart = false
        offlineTranscription.open()
    }
    Connections {
        target: root.audioLevelTest
        ignoreUnknownSignals: true
        function onChanged() { root.openTranscriptionWhenCaptureFree() }
        function onStoppingChanged() { root.openTranscriptionWhenCaptureFree() }
        function onActiveChanged() { root.openTranscriptionWhenCaptureFree() }
    }
    function refreshDevices() {
        if (audioDevices)
            audioDevices.refresh()
    }
    function syncDeviceSelection() {
        if (deviceSelectorsReady && audioLevelTest)
            audioLevelTest.setDevices(microphoneInput.selectedDeviceId, systemInput.selectedDeviceId)
        if (deviceSelectorsReady && offlineTranscription)
            offlineTranscription.setDevices(microphoneInput.selectedDeviceId, systemInput.selectedDeviceId)
    }
    function scheduleDeviceSelectionSync() {
        // 两路列表可能在同一轮同时更新；合并后再通知测试控制器，避免中间状态。
        if (deviceSelectorsReady)
            Qt.callLater(root.syncDeviceSelection)
    }
    function restoreDeviceSelection(field) {
        // 优先按稳定 ID 保留选择；设备消失时回退到默认设备，再回退到首项。
        const devices = field.model
        let selected = -1
        let defaultIndex = -1
        for (let i = 0; i < devices.length; ++i) {
            if (devices[i].id === field.selectedDeviceId)
                selected = i
            if (defaultIndex < 0 && devices[i].isDefault)
                defaultIndex = i
        }
        if (selected < 0)
            selected = defaultIndex >= 0 ? defaultIndex : (devices.length > 0 ? 0 : -1)
        field.currentIndex = selected
        const id = selected >= 0 ? devices[selected].id : ""
        if (field.selectedDeviceId !== id) {
            field.selectedDeviceId = id
            scheduleDeviceSelectionSync()
        }
    }

    component LevelMeter: ProgressBar {
        id: meter
        from: 0
        to: 1
        implicitHeight: 16
        background: Item {}
        contentItem: Row {
            id: segments
            // 保持两行短竖条的尺寸一致，宽度变化时只调整条间距。
            readonly property int segmentCount: 15
            readonly property real segmentWidth: Math.min(7, Math.max(0, (width - 2 * (segmentCount - 1)) / segmentCount))
            spacing: Math.max(0, (width - segmentWidth * segmentCount) / (segmentCount - 1))
            Repeater {
                model: segments.segmentCount
                delegate: Rectangle {
                    required property int index
                    width: segments.segmentWidth
                    height: segments.height
                    radius: width / 2
                    color: index < Math.ceil(meter.visualPosition * segments.segmentCount) ? "#626267" : "#dcdce0"
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
            }
        }
    }

    PageHeader {
        Layout.fillWidth: true
        title: qsTr("音频与转写")
        subtitle: qsTr("管理本地转写引擎与双方音频输入。")
    }
    FormCard {
        Layout.fillWidth: true
        framed: false
        spacing: 20
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("转写引擎")
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                StatusBadge {
                    text: root.engineLabel
                }
                Item {
                    Layout.fillWidth: true
                }
                AppButton {
                    objectName: "testTranscriptionButton"
                    text: root.offlineTranscription ? qsTr("测试转写") : qsTr("测试转写（演示）")
                    variant: "secondary"
                    enabled: !root.offlineTranscription || root.engineStatus === "ready"
                    onClicked: {
                        if (root.offlineTranscription) {
                            if (root.audioLevelTest && (root.audioLevelTest.active || root.audioLevelTest.stopping)) {
                                root.pendingTranscriptionStart = true
                                root.audioLevelTest.stop()
                                root.openTranscriptionWhenCaptureFree()
                            } else root.offlineTranscription.open()
                        } else root.controller.transcriptionTest.open()
                    }
                }
                AppButton {
                    objectName: "engineActionButton"
                    text: root.offlineTranscription ? qsTr("引擎配置") : root.engineStatus === "ready" ? qsTr("停止引擎") : root.engineStatus === "starting" ? qsTr("正在启动…") : qsTr("启动引擎")
                    variant: root.engineStatus === "ready" ? "secondary" : "primary"
                    enabled: root.offlineTranscription || root.engineStatus !== "starting"
                    onClicked: root.offlineTranscription ? (root.controller.route = "offline") : root.controller.toggleEngine()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("对方的声音")
            description: qsTr("通过系统音频接收")
            SelectField {
                id: systemInput
                objectName: "systemDeviceField"
                property string selectedDeviceId: ""
                Layout.fillWidth: true
                model: root.audioDevices ? root.audioDevices.systemDevices : []
                textRole: "displayName"
                enabled: count > 0
                displayText: count > 0 ? currentText : qsTr("暂无可用设备")
                onModelChanged: root.restoreDeviceSelection(systemInput)
                onActivated: function(index) {
                    selectedDeviceId = model[index].id
                    root.scheduleDeviceSelectionSync()
                    console.log(model[index].name)
                }
                Accessible.name: qsTr("系统音频设备")
            }
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("你的声音")
            description: qsTr("通过麦克风接收")
            SelectField {
                id: microphoneInput
                objectName: "microphoneDeviceField"
                property string selectedDeviceId: ""
                Layout.fillWidth: true
                model: root.audioDevices ? root.audioDevices.microphoneDevices : []
                textRole: "displayName"
                enabled: count > 0
                displayText: count > 0 ? currentText : qsTr("暂无可用设备")
                onModelChanged: root.restoreDeviceSelection(microphoneInput)
                onActivated: function(index) {
                    selectedDeviceId = model[index].id
                    root.scheduleDeviceSelectionSync()
                    console.log(model[index].name)
                }
                Accessible.name: qsTr("麦克风设备")
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.border
        }
        SettingRow {
            Layout.fillWidth: true
            label: qsTr("收音测试")
            Rectangle {
                objectName: "audioLevelMeterPanel"
                Layout.fillWidth: true
                implicitHeight: Math.max(levelGrid.implicitHeight, testButton.implicitHeight) + 24
                color: Theme.surface
                border.color: Theme.border
                radius: Theme.radius
                RowLayout {
                    x: 12; y: 12
                    width: Math.max(0, parent.width - 24)
                    height: Math.max(levelGrid.implicitHeight, testButton.implicitHeight)
                    spacing: 10
                    GridLayout {
                        id: levelGrid
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 10
                        rowSpacing: 14

                        Label {
                            text: qsTr("系统声音")
                            Layout.preferredWidth: 58
                            color: Theme.text; font.pixelSize: 12
                        }
                        LevelMeter {
                            objectName: "systemCaptureLevel"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 60
                            value: root.audioLevelTest ? root.audioLevelTest.systemLevel : 0
                            Accessible.name: qsTr("系统声音电平")
                        }
                        Label {
                            objectName: "systemCaptureStatus"
                            text: root.audioLevelTest && root.audioLevelTest.systemStatus ? root.audioLevelTest.systemStatus : qsTr("等待测试")
                            Layout.preferredWidth: 110
                            Layout.maximumWidth: 110
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignRight
                            color: Theme.muted; font.pixelSize: 12
                        }

                        Label {
                            text: qsTr("麦克风")
                            Layout.preferredWidth: 58
                            color: Theme.text; font.pixelSize: 12
                        }
                        LevelMeter {
                            objectName: "microphoneCaptureLevel"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 60
                            value: root.audioLevelTest ? root.audioLevelTest.microphoneLevel : 0
                            Accessible.name: qsTr("麦克风电平")
                        }
                        Label {
                            objectName: "microphoneCaptureStatus"
                            text: root.audioLevelTest && root.audioLevelTest.microphoneStatus ? root.audioLevelTest.microphoneStatus : qsTr("等待测试")
                            Layout.preferredWidth: 110
                            Layout.maximumWidth: 110
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignRight
                            color: Theme.muted; font.pixelSize: 12
                        }
                    }
                    IconButton {
                        id: testButton
                        objectName: "audioLevelTestButton"
                        iconName: root.audioLevelTest && (root.audioLevelTest.active || root.audioLevelTest.stopping) ? "stop" : "play"
                        text: root.audioLevelTest && root.audioLevelTest.active ? qsTr("停止测试") :
                              root.audioLevelTest && root.audioLevelTest.stopping ? qsTr("正在停止…") : qsTr("测试收音")
                        enabled: root.audioLevelTest !== null &&
                                 (root.audioLevelTest.active || (!root.audioLevelTest.stopping &&
                                  (microphoneInput.selectedDeviceId !== "" || systemInput.selectedDeviceId !== "")))
                        background: Rectangle {
                            radius: width / 2
                            color: testButton.down ? "#e4e4e7" : testButton.hovered ? Theme.hover : "transparent"
                            border.color: testButton.visualFocus ? Theme.ring : Theme.border
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        onClicked: {
                            if (root.audioLevelTest.active)
                                root.audioLevelTest.stop()
                            else
                                root.audioLevelTest.start(microphoneInput.selectedDeviceId, systemInput.selectedDeviceId)
                        }
                    }
                }
            }
        }
    }
    Connections {
        target: systemInput.popup
        function onAboutToShow() { root.refreshDevices() }
    }
    Connections {
        target: microphoneInput.popup
        function onAboutToShow() { root.refreshDevices() }
    }
}
