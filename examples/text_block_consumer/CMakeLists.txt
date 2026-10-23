cmake_minimum_required(VERSION 3.24)

project(TextBlockConsumer LANGUAGES CXX)

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

get_filename_component(_default_text_block_source
    "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/text_block" ABSOLUTE)
set(TEXT_BLOCK_SOURCE_DIR "${_default_text_block_source}" CACHE PATH
    "Path to the TextBlock module source directory")
if(NOT EXISTS "${TEXT_BLOCK_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set TEXT_BLOCK_SOURCE_DIR to the path of modules/text_block")
endif()

option(TEXT_BLOCK_BUILD_TESTS "Build TextBlock module tests" OFF)
if(TEXT_BLOCK_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory("${TEXT_BLOCK_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/text_block")

# The repository example shares its font bundle when available. A consumer
# copied elsewhere still needs only TextBlock and Qt; an empty path opts out.
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

qt_add_executable(text_block_consumer main.cpp)
qt_add_qml_module(text_block_consumer
    URI TextBlockConsumer
    VERSION 1.0
    QML_FILES Main.qml TextCopyActions.qml
    SOURCES text_clipboard.h text_clipboard.cpp
)

target_compile_features(text_block_consumer PRIVATE cxx_std_23)
target_link_libraries(text_block_consumer PRIVATE Qt6::Quick Qt6::QuickControls2 text_block text_blockplugin)
if(CHUNFE_FONTS_SOURCE_DIR)
    target_link_libraries(text_block_consumer PRIVATE chunfe_fonts)
    target_compile_definitions(text_block_consumer PRIVATE TEXT_BLOCK_PROJECT_FONTS)
endif()
