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

if(TEXT_BLOCK_BUILD_TESTS)
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
        CHUNFE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        TEXT_BLOCK_LAB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        CHUNFE_BINARY_DIR="${CMAKE_BINARY_DIR}"
        CHUNFE_QMLLINT="$<TARGET_FILE:Qt6::qmllint>")
    target_link_libraries(main_qml_tests PRIVATE Qt6::Test Qt6::QuickControls2
        chunfe_fonts text_block text_blockplugin)
    add_dependencies(main_qml_tests text_block_lab)
    add_test(NAME main_qml_tests COMMAND main_qml_tests)
    set_tests_properties(main_qml_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
