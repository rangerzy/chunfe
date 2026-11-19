find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

qt_add_qml_module(transcription_test
    URI Chunfe.AudioTest
    VERSION 1.0
    STATIC
    QML_FILES TranscriptionTestPanel.qml AudioChannelStatus.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic QtQuick.Layouts Chunfe.Ui
)
target_link_libraries(transcription_test PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    ui_controls ui_controlsplugin)
