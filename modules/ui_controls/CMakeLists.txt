find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(ui_controls
    URI Chunfe.Ui
    VERSION 1.0
    STATIC
    QML_FILES
        Theme.qml
        AppButton.qml
        IconButton.qml
        AppField.qml
        StatusBadge.qml
        PageHeader.qml
        SettingRow.qml
        AppIcon.qml
        IconParkData.js
    RESOURCES icons/LICENSE icons/NOTICE icons/SOURCE.json
    DEPENDENCIES QtQuick QtQuick.Controls.Basic QtQuick.Layouts
)
target_link_libraries(ui_controls PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
