cmake_minimum_required(VERSION 3.24)

project(TextBlockLab LANGUAGES CXX)

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

get_filename_component(_default_fonts_source
    "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/fonts" ABSOLUTE)
set(CHUNFE_FONTS_SOURCE_DIR "${_default_fonts_source}" CACHE PATH
    "Path to the font bundle required by TextBlockLab")
if(NOT EXISTS "${CHUNFE_FONTS_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Set CHUNFE_FONTS_SOURCE_DIR to the path of resources/fonts")
endif()

option(TEXT_BLOCK_BUILD_TESTS "Build TextBlock module tests" OFF)
option(TEXT_BLOCK_LAB_BUILD_TESTS "Build TextBlockLab tests" OFF)
if(TEXT_BLOCK_BUILD_TESTS OR TEXT_BLOCK_LAB_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory("${TEXT_BLOCK_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/text_block")
add_subdirectory("${CHUNFE_FONTS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/fonts")

set(text_block_lab_qml_files Main.qml AppActionButton.qml TextCopyActions.qml Test.qml)

qt_add_executable(text_block_lab main.cpp)
qt_add_qml_module(text_block_lab
    URI TextBlockLab
    VERSION 1.0
    QML_FILES ${text_block_lab_qml_files}
    SOURCES experiment_metrics.h experiment_metrics.cpp
    SOURCES text_clipboard.h text_clipboard.cpp
)
target_compile_features(text_block_lab PRIVATE cxx_std_23)
target_link_libraries(text_block_lab PRIVATE
    Qt6::Quick Qt6::QuickControls2 text_block text_blockplugin chunfe_fonts)
target_compile_definitions(text_block_lab PRIVATE
    $<$<CONFIG:Debug,RelWithDebInfo>:QT_QML_DEBUG>
)
set_target_properties(text_block_lab PROPERTIES WIN32_EXECUTABLE TRUE)

if(TEXT_BLOCK_LAB_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    qt_add_executable(experiment_metrics_tests
        tests/experiment_metrics_tests.cpp experiment_metrics.h experiment_metrics.cpp)
    target_include_directories(experiment_metrics_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(experiment_metrics_tests PRIVATE cxx_std_23)
    target_link_libraries(experiment_metrics_tests PRIVATE Qt6::Test text_block text_blockplugin)
    add_test(NAME experiment_metrics_tests COMMAND experiment_metrics_tests)
    set_tests_properties(experiment_metrics_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(text_copy_actions_tests
        tests/text_copy_actions_tests.cpp text_clipboard.h text_clipboard.cpp)
    target_include_directories(text_copy_actions_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(text_copy_actions_tests PRIVATE cxx_std_23)
    target_compile_definitions(text_copy_actions_tests PRIVATE
        TEXT_COPY_ACTIONS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(text_copy_actions_tests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2)
    add_test(NAME text_copy_actions_tests COMMAND text_copy_actions_tests)
    set_tests_properties(text_copy_actions_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(main_qml_tests tests/main_qml_tests.cpp
        experiment_metrics.h experiment_metrics.cpp text_clipboard.h text_clipboard.cpp)
    # The adjacent app's qt.conf exposes the built module, whose qmldir prefers
    # resource URLs. Supply the same QML assets in this separate test process.
    qt_add_resources(main_qml_tests text_block_lab_test_qml
        PREFIX "/qt/qml/TextBlockLab"
        FILES ${text_block_lab_qml_files})
    target_include_directories(main_qml_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(main_qml_tests PRIVATE cxx_std_23)
    target_compile_definitions(main_qml_tests PRIVATE
        TEXT_BLOCK_SOURCE_DIR="${TEXT_BLOCK_SOURCE_DIR}"
        TEXT_BLOCK_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        TEXT_BLOCK_LAB_BINARY_DIR="${CMAKE_BINARY_DIR}"
        TEXT_BLOCK_LAB_QMLLINT="$<TARGET_FILE:Qt6::qmllint>")
    target_link_libraries(main_qml_tests PRIVATE Qt6::Test Qt6::QuickControls2
        chunfe_fonts text_block text_blockplugin)
    add_dependencies(main_qml_tests text_block_lab)
    add_test(NAME main_qml_tests COMMAND main_qml_tests)
    set_tests_properties(main_qml_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
