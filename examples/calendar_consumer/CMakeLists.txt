cmake_minimum_required(VERSION 3.24)

project(CalendarConsumer LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ChunfeQmlTooling.cmake" OPTIONAL)
if(COMMAND chunfe_setup_qml_tooling)
    chunfe_setup_qml_tooling()
else()
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
    set(QML_IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}" CACHE STRING "QML tooling import paths")
    set(QT_QML_GENERATE_QMLLS_INI ON)
endif()

get_filename_component(_default_calendar_source
    "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/calendar" ABSOLUTE)
set(CALENDAR_SOURCE_DIR "${_default_calendar_source}" CACHE PATH
    "Path to the Calendar module source directory")
if(NOT EXISTS "${CALENDAR_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set CALENDAR_SOURCE_DIR to the path of modules/calendar")
endif()

option(CALENDAR_BUILD_TESTS "Build Calendar module tests" OFF)
if(CALENDAR_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory("${CALENDAR_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/calendar")

# The font bundle is optional; a copied consumer needs only Calendar and Qt.
set(_default_fonts_source "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/fonts/CMakeLists.txt")
    get_filename_component(_default_fonts_source
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/fonts" ABSOLUTE)
endif()
set(CHUNFE_FONTS_SOURCE_DIR "${_default_fonts_source}" CACHE PATH
    "Optional font bundle directory; leave empty to use the application's system font")
if(CHUNFE_FONTS_SOURCE_DIR)
    if(NOT EXISTS "${CHUNFE_FONTS_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Set CHUNFE_FONTS_SOURCE_DIR to a font bundle directory, or leave it empty")
    endif()
    add_subdirectory("${CHUNFE_FONTS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/fonts")
endif()

qt_add_executable(calendar_consumer main.cpp)
qt_add_qml_module(calendar_consumer
    URI CalendarConsumer
    VERSION 1.0
    QML_FILES Main.qml
)

target_compile_features(calendar_consumer PRIVATE cxx_std_23)
target_link_libraries(calendar_consumer PRIVATE Qt6::Quick Qt6::QuickControls2 calendar calendarplugin)
if(CHUNFE_FONTS_SOURCE_DIR)
    target_link_libraries(calendar_consumer PRIVATE chunfe_fonts)
    target_compile_definitions(calendar_consumer PRIVATE CALENDAR_PROJECT_FONTS)
endif()
