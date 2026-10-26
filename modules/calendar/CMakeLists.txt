find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

# The liquid animation is an implementation detail of PastDateCalendar.
set_source_files_properties(LiquidSelection.qml PROPERTIES QT_QML_INTERNAL_TYPE TRUE)
qt_add_qml_module(calendar
    URI Calendar
    VERSION 1.0
    STATIC
    QML_FILES PastDateCalendar.qml LiquidSelection.qml
    DEPENDENCIES QtQuick QtQuick.Controls.Basic QtQuick.Layouts QtQuick.Shapes
)
target_link_libraries(calendar PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

option(CALENDAR_BUILD_TESTS "Build Calendar module tests" OFF)
if(CALENDAR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
