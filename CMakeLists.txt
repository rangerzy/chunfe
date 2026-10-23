cmake_minimum_required(VERSION 3.24)

project(Chunfe VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12)
include(cmake/ChunfeQmlTooling.cmake)
chunfe_setup_qml_tooling()
include(GNUInstallDirs)

option(CHUNFE_BUILD_TESTS "Build Chunfe application tests" OFF)
option(TEXT_BLOCK_BUILD_TESTS "Build TextBlock module tests" ${CHUNFE_BUILD_TESTS})
if(CHUNFE_BUILD_TESTS OR TEXT_BLOCK_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(modules)
add_subdirectory(resources/fonts)
add_subdirectory(src)

if(CHUNFE_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    qt_add_executable(application_fonts_tests tests/application_fonts_tests.cpp)
    target_compile_features(application_fonts_tests PRIVATE cxx_std_23)
    target_link_libraries(application_fonts_tests PRIVATE
        Qt6::Test Qt6::QuickControls2 chunfe_fonts text_block text_blockplugin)
    add_test(NAME application_fonts_tests COMMAND application_fonts_tests)
    set_tests_properties(application_fonts_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/version.txt" CONTENT "${PROJECT_VERSION}\n")
