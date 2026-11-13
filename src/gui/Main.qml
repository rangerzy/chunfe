pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Chunfe.Ui
import Chunfe.Transcript
import Chunfe.Answer
import "mock"
import "pages"
import "shell"

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    property var audioDevices: null
    property var audioLevelTest: null
    property var offlineTranscription: null
    property bool applicationQuitting: false
    readonly property var transcriptionTest: offlineTranscription || demo.transcriptionTest
    readonly property alias demoController: demo
    readonly property alias transcriptVisible: transcriptWindow.visible
    readonly property alias answerVisible: answerWindow.visible

    width: 820
    height: 560
    minimumWidth: 760
    minimumHeight: 520
    visible: !applicationQuitting && !demo.interviewActive
    title: qsTr("春风")
    color: Theme.background
    font.pixelSize: 14
    onClosing: {
        if (!applicationQuitting)
            Qt.quit()
    }
    onApplicationQuittingChanged: {
        if (applicationQuitting) {
            transcriptionTest.close()
            demo.cancelChecks()
            demo.finishInterview()
        }
    }

    function closeInterviewWindow(close) {
        if (!applicationQuitting && demo.interviewActive) {
            close.accepted = false
            demo.finishInterview()
        }
    }

    function positionInterviewWindows() {
        const available = Qt.rect(window.screen.virtualX, window.screen.virtualY, window.screen.width, window.screen.height)
        transcriptWindow.width = Math.round(Math.min(480, (available.width - 72) * 0.54))
        answerWindow.width = Math.min(400, available.width - transcriptWindow.width - 72)
        const startX = Math.round(available.x + Math.max(24, (available.width - transcriptWindow.width - answerWindow.width - 24) / 2))
        const startY = Math.round(available.y + Math.min(100, Math.max(40, (available.height - answerWindow.height) / 3)))
        transcriptWindow.x = startX; transcriptWindow.y = startY
        answerWindow.x = startX + transcriptWindow.width + 24; answerWindow.y = startY
    }

    DemoController {
        id: demo
        onInterviewStarted: { window.transcriptionTest.close(); window.positionInterviewWindows() }
        onInterviewFinished: {
            if (!window.applicationQuitting) {
                window.show()
                window.raise()
                window.requestActivate()
            }
        }
        onNotification: function(message, severity) { toast.showNotification(message, severity) }
        onPreflightVisibleChanged: { if (preflightVisible) checks.open(); else checks.close() }
    }

    Sidebar {
        id: rail
        objectName: "sidebar"
        width: 64; height: parent.height; z: 10
        controller: demo
    }
    Loader {
        id: pageLoader
        objectName: "pageLoader"
        anchors.left: rail.right; anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        sourceComponent: ({
            home: homePage,
            background: backgroundPage,
            model: modelPage,
            audio: audioPage,
            offline: offlinePage,
            history: historyPage,
            record: recordPage,
            about: aboutPage
        })[demo.route] || homePage
    }
    Component { id: homePage; HomePage { controller: demo } }
    Component { id: backgroundPage; BackgroundPage { controller: demo } }
    Component { id: modelPage; ModelPage { controller: demo } }
    Component { id: audioPage; AudioPage { controller: demo; audioDevices: window.audioDevices; audioLevelTest: window.audioLevelTest; offlineTranscription: window.offlineTranscription } }
    Component { id: offlinePage; OfflineTranscriptionPage { controller: window.offlineTranscription } }
    Component { id: historyPage; HistoryPage { controller: demo } }
    Component { id: recordPage; RecordPage { controller: demo } }
    Component { id: aboutPage; AboutPage {} }

    PreflightDialog { id: checks; objectName: "preflightDialog"; anchors.centerIn: Overlay.overlay; controller: demo }
    TranscriptionTestDialog { anchors.centerIn: Overlay.overlay; controller: window.transcriptionTest }
    Connections {
        target: demo
        function onRouteChanged() {
            if (demo.route !== "audio" && window.offlineTranscription)
                window.offlineTranscription.close()
        }
        function onPreflightVisibleChanged() {
            if (demo.preflightVisible && window.offlineTranscription)
                window.offlineTranscription.close()
        }
    }

    NotificationToast {
        id: toast
        objectName: "notificationToast"
        z: 30
        anchors.horizontalCenter: pageLoader.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        maximumWidth: Math.min(480, pageLoader.width - 40)
    }

    Window {
        id: transcriptWindow
        objectName: "transcriptWindow"
        transientParent: null
        title: qsTr("春风 · 实时对话")
        // Cocoa Tool panels hide when another app becomes active. A regular
        // top-level window keeps these dialogue hints visible on macOS.
        flags: (Qt.platform.os === "osx" ? Qt.Window : Qt.Tool) | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: Math.min(480, screen.width - 40)
        height: transcriptPanel.implicitHeight
        visible: demo.interviewActive
        onClosing: function(close) { window.closeInterviewWindow(close) }
        TranscriptPanel {
            id: transcriptPanel
            anchors.fill: parent
            messages: demo.messages; paused: demo.paused; collapsed: demo.transcriptCollapsed; elapsedText: demo.elapsedText
            logoSource: "qrc:/branding/chunfe-mark.svg"
            onPauseRequested: demo.togglePause()
            onCollapseRequested: demo.transcriptCollapsed = !demo.transcriptCollapsed
            onFinishRequested: demo.finishInterview()
            onDragRequested: transcriptWindow.startSystemMove()
        }
    }
    Window {
        id: answerWindow
        objectName: "answerWindow"
        transientParent: null
        title: qsTr("春风 · 回应建议")
        flags: (Qt.platform.os === "osx" ? Qt.Window : Qt.Tool) | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: Math.min(400, screen.width - 40)
        height: answerPanel.implicitHeight
        visible: demo.interviewActive
        onClosing: function(close) { window.closeInterviewWindow(close) }
        AnswerPanel {
            id: answerPanel
            anchors.fill: parent
            question: demo.question; triggerKind: demo.triggerKind; answer: demo.answer; generating: demo.generating; pendingQuestion: demo.pendingQuestion; collapsed: demo.answerCollapsed
            onCollapseRequested: demo.answerCollapsed = !demo.answerCollapsed
            onDragRequested: answerWindow.startSystemMove()
        }
    }
}
