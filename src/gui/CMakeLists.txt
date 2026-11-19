qt_add_qml_module(chunfe_gui
    URI Chunfe
    STATIC
    CLASS_NAME ChunfePlugin
    QML_FILES
        Main.qml
        mock/DemoController.qml mock/DemoData.js mock/TranscriptionTestController.qml
        shell/Sidebar.qml shell/NavItem.qml shell/PreflightDialog.qml shell/TranscriptionTestDialog.qml
        shell/NotificationToast.qml shell/ModelTestChatDialog.qml
        pages/HomePage.qml pages/BackgroundPage.qml pages/ModelPage.qml
        pages/AudioPage.qml pages/OfflineTranscriptionPage.qml pages/HistoryPage.qml pages/RecordPage.qml pages/AboutPage.qml
        pages/PageFrame.qml pages/FormCard.qml pages/FieldLabel.qml pages/SelectField.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic QtQuick.Layouts Chunfe.Ui Chunfe.Transcript Chunfe.Answer Chunfe.AudioTest
)
target_compile_features(chunfe_gui PRIVATE cxx_std_23)
target_link_libraries(chunfe_gui PUBLIC Qt6::Quick Qt6::QuickControls2
    ui_controls ui_controlsplugin interview_transcript interview_transcriptplugin
    answer_panel answer_panelplugin transcription_test transcription_testplugin)

qt_add_resources(chunfe_gui chunfe_branding
    PREFIX "/branding"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/branding"
    FILES
        "../../resources/branding/chunfe-mark.svg"
        "../../resources/branding/chunfe-mark-light.svg"
        "../../resources/branding/chunfe-app.svg")

if(CHUNFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
