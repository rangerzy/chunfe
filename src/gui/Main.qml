pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
    property var modelConnection: null
    property var modelTestChat: null
    property var backgroundDocument: null
    property var backgroundAnalysis: null
    property var conversationStore: null
    property var conversationAdvice: null
    property bool applicationQuitting: false
    property bool allowQuitWithUnsavedConversation: false
    readonly property bool conversationBlocksQuit: demo.unsavedRecords.length > 0 && !allowQuitWithUnsavedConversation
    readonly property bool conversationQuitPending: applicationQuitting && !demo.isDemo && demo.interviewActive
    readonly property bool showsAnswerWindow: demo.isDemo || conversationAdvice !== null
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
    onClosing: function(close) {
        if (!applicationQuitting) {
            close.accepted = false
            Qt.quit()
        }
    }
    onApplicationQuittingChanged: {
        if (applicationQuitting) {
            if (demo.route === "model")
                window.saveModelDraft()
            modelTestChatDialog.close()
            backgroundAnalysis?.cancelAnalysis()
            demo.cancelChecks()
            demo.finishInterview()
            if (transcriptionTest.opened && !offlineTranscription?.conversationActive)
                transcriptionTest.close()
        }
    }

    function saveModelDraft() {
        if (modelConnection?.configurationDirty)
            modelConnection.saveModel()
    }
    function showUnsavedConversationQuitDialog() {
        applicationQuitting = false
        unsavedConversationQuitDialog.open()
        requestActivate()
    }

    function closeInterviewWindow(close) {
        if (!applicationQuitting && demo.interviewActive) {
            close.accepted = false
            demo.finishInterview()
        }
    }

    function positionInterviewWindows() {
        const available = Qt.rect(window.screen.virtualX, window.screen.virtualY, window.screen.width, window.screen.height)
        if (!showsAnswerWindow) {
            transcriptWindow.width = Math.min(480, available.width - 72)
            transcriptWindow.x = Math.round(available.x + (available.width - transcriptWindow.width) / 2)
            transcriptWindow.y = Math.round(available.y + Math.min(100, Math.max(24, (available.height - transcriptWindow.height) / 3)))
            return
        }
        const gap = 24
        const panelWidth = Math.max(2, available.width - 2 * 24 - gap)
        transcriptWindow.width = Math.min(480, Math.round(panelWidth * 0.54))
        answerWindow.width = Math.min(400, panelWidth - transcriptWindow.width)
        const startX = Math.round(available.x + (available.width - transcriptWindow.width - answerWindow.width - gap) / 2)
        const startY = Math.round(available.y + Math.min(100, Math.max(24, (available.height - Math.max(transcriptWindow.height, answerWindow.height)) / 3)))
        transcriptWindow.x = startX; transcriptWindow.y = startY
        answerWindow.x = startX + transcriptWindow.width + gap; answerWindow.y = startY
    }

    DemoController {
        id: demo
        conversationStore: window.conversationStore
        offlineTranscription: window.offlineTranscription
        audioLevelTest: window.audioLevelTest
        conversationAdvice: window.conversationAdvice
        backgroundText: window.backgroundDocument?.backgroundText ?? ""
        onInterviewStarted: {
            if (demo.isDemo && window.transcriptionTest.opened)
                window.transcriptionTest.close()
            window.positionInterviewWindows()
        }
        onInterviewFinished: {
            if (window.applicationQuitting && !demo.isDemo) {
                Qt.callLater(() => {
                    if (window.applicationQuitting)
                        Qt.quit()
                })
            } else if (!window.applicationQuitting) {
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
    Component {
        id: homePage
        HomePage {
            controller: demo
            modelVerificationStatus: window.modelConnection?.verificationStatus ?? "unconfigured"
            modelVerificationRestored: window.modelConnection?.verificationRestored ?? false
            offlineEngineStatus: window.offlineTranscription?.engineStatus ?? "stopped"
        }
    }
    Component {
        id: backgroundPage
        BackgroundPage {
            controller: window.backgroundDocument
            analysisController: window.backgroundAnalysis
        }
    }
    Component {
        id: modelPage
        ModelPage {
            controller: window.modelConnection
            testChatAvailable: window.modelTestChat !== null
            onTestChatRequested: {
                if (!modelTestChatDialog.visible && window.modelTestChat?.beginSession())
                    modelTestChatDialog.open()
            }
        }
    }
    Component { id: audioPage; AudioPage { controller: demo; audioDevices: window.audioDevices; audioLevelTest: window.audioLevelTest; offlineTranscription: window.offlineTranscription } }
    Component { id: offlinePage; OfflineTranscriptionPage { controller: window.offlineTranscription } }
    Component { id: historyPage; HistoryPage { controller: demo } }
    Component { id: recordPage; RecordPage { controller: demo } }
    Component { id: aboutPage; AboutPage {} }

    PreflightDialog { id: checks; objectName: "preflightDialog"; anchors.centerIn: Overlay.overlay; controller: demo }
    Dialog {
        id: unsavedConversationQuitDialog
        objectName: "unsavedConversationQuitDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 40 : 440)
        modal: true
        focus: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        title: qsTr("对话记录尚未保存")
        background: Rectangle {
            color: Theme.background
            border.color: Theme.border
            radius: Theme.radius
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: qsTr("尚未保存的转写会在退出时丢失。你可以返回应用重试保存。")
                color: Theme.text
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    objectName: "stayWithUnsavedConversationButton"
                    text: qsTr("返回应用")
                    onClicked: unsavedConversationQuitDialog.close()
                }
                AppButton {
                    variant: "secondary"
                    text: qsTr("仍然退出")
                    onClicked: {
                        window.allowQuitWithUnsavedConversation = true;
                        unsavedConversationQuitDialog.close();
                        Qt.quit();
                    }
                }
            }
        }
    }
    TranscriptionTestDialog { anchors.centerIn: Overlay.overlay; controller: window.transcriptionTest }
    ModelTestChatDialog {
        id: modelTestChatDialog
        parent: Overlay.overlay
        controller: window.modelTestChat
    }
    Connections {
        target: window.modelConnection
        function onNotification(message, severity) {
            toast.showNotification(message, severity)
        }
    }
    Connections {
        id: routeChanges
        target: demo
        property string previousRoute: "home"
        function onRouteChanged() {
            if (routeChanges.previousRoute === "model" && demo.route !== "model") {
                modelTestChatDialog.close()
                window.saveModelDraft()
            }
            routeChanges.previousRoute = demo.route
            if (demo.route !== "audio" && window.offlineTranscription?.opened
                    && !window.offlineTranscription.conversationActive)
                window.offlineTranscription.close()
        }
        function onPreflightVisibleChanged() {
            if (demo.isDemo && demo.preflightVisible && window.offlineTranscription?.opened
                    && !window.offlineTranscription.conversationActive)
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
        title: demo.isDemo ? qsTr("春风 · 对话演示") : qsTr("春风 · 实时转写")
        // Cocoa Tool panels hide when another app becomes active. A regular
        // top-level window keeps these dialogue hints visible on macOS.
        flags: (Qt.platform.os === "osx" ? Qt.Window : Qt.Tool) | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: Math.min(480, screen.width - 40)
        height: Math.min(transcriptPanel.implicitHeight, screen.height - 24)
        visible: demo.interviewActive
        onClosing: function(close) { window.closeInterviewWindow(close) }
        TranscriptPanel {
            id: transcriptPanel
            anchors.fill: parent
            messages: demo.isDemo ? demo.messages : demo.liveConversationMessages
            paused: demo.paused
            finishing: demo.interviewEnding
            collapsed: demo.transcriptCollapsed
            elapsedText: demo.elapsedText
            statusText: demo.isDemo ? qsTr("春风正在聆听") : demo.nativeConversationStatus
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
        title: demo.isDemo ? qsTr("春风 · 回应建议演示") : qsTr("春风 · 回应建议")
        flags: (Qt.platform.os === "osx" ? Qt.Window : Qt.Tool) | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        color: "transparent"
        width: Math.min(400, screen.width - 40)
        height: Math.min(answerPanel.implicitHeight, screen.height - 24)
        visible: window.showsAnswerWindow && demo.interviewActive
        onClosing: function(close) { window.closeInterviewWindow(close) }
        AnswerPanel {
            id: answerPanel
            objectName: "conversationAnswerPanel"
            anchors.fill: parent
            heading: demo.isDemo ? qsTr("回应建议（演示）") : qsTr("回应建议")
            question: demo.isDemo ? demo.question : window.conversationAdvice?.question ?? ""
            triggerKind: demo.isDemo ? demo.triggerKind : window.conversationAdvice?.triggerKind ?? ""
            answer: demo.isDemo ? demo.answer : window.conversationAdvice?.answer ?? ""
            generating: demo.isDemo ? demo.generating : window.conversationAdvice?.generating ?? false
            pendingQuestion: demo.isDemo ? demo.pendingQuestion : ""
            statusMessage: demo.isDemo ? "" : window.conversationAdvice?.statusMessage ?? ""
            collapsed: demo.answerCollapsed
            onCollapseRequested: demo.answerCollapsed = !demo.answerCollapsed
            onDragRequested: answerWindow.startSystemMove()
        }
    }
}
