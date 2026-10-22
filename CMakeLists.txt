cmake_minimum_required(VERSION 3.24)

project(Chunfe VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12)
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
# Qt Creator's classic code model reads this cache entry separately from qmlls.
list(PREPEND QML_IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}")
list(REMOVE_DUPLICATES QML_IMPORT_PATH)
set(QML_IMPORT_PATH "${QML_IMPORT_PATH}" CACHE STRING "QML import paths for Qt Creator" FORCE)

option(TEXT_BLOCK_BUILD_TESTS "Build TextBlock module and example tests" OFF)
option(TEXT_BLOCK_BUILD_LAB "Build the TextBlock experiment example" ON)
if(TEXT_BLOCK_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(modules)
add_subdirectory(resources/fonts)

# The real Chunfe application entry point will be added separately.
if(TEXT_BLOCK_BUILD_LAB)
    add_subdirectory(examples/text_block_lab)
endif()

if(TEXT_BLOCK_BUILD_TESTS)
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

# Qt 6.12 generates this metadata from a configure-time fragment, but its
# build rule omits the fragment dependency. Keep editor import paths/types
# current after modules or resources change, including incremental builds.
function(chunfe_track_qmlls_metadata)
    set(metadata "${CMAKE_BINARY_DIR}/.qt/.qmlls.build.ini")
    if(TARGET generate_qmlls_build_ini_file AND EXISTS "${metadata}.part")
        add_custom_command(OUTPUT "${metadata}" APPEND DEPENDS "${metadata}.part")
    endif()
endfunction()
cmake_language(DEFER CALL chunfe_track_qmlls_metadata)
