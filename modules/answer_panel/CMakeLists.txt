find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

qt_add_qml_module(answer_panel
    URI Chunfe.Answer
    VERSION 1.0
    STATIC
    QML_FILES AnswerPanel.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic Chunfe.Ui TextBlock
)
target_link_libraries(answer_panel PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    ui_controls ui_controlsplugin text_block text_blockplugin)
