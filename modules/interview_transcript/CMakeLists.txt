find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

qt_add_qml_module(interview_transcript
    URI Chunfe.Transcript
    VERSION 1.0
    STATIC
    QML_FILES TranscriptPanel.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic Chunfe.Ui TextBlock
)
target_link_libraries(interview_transcript PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    ui_controls ui_controlsplugin text_block text_blockplugin)
