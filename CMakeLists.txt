cmake_minimum_required(VERSION 4.3.6)

set(CMAKE_AUTOMOC ON) # 自动处理 MOC 宏（如 Q_OBJECT）
set(CMAKE_AUTOUIC ON) # 自动处理 .ui 界面文件
set(CMAKE_AUTORCC ON) # 自动处理 .qrc 资源文件

include(cmake/AppVersion.cmake)
project(Chunfe VERSION ${APP_VERSION} LANGUAGES C CXX)
if(APPLE)
    # The macOS app and audio plugin both compile Objective-C++ sources.
    enable_language(OBJCXX)
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12 I18N_SOURCE_LANGUAGE zh-Hans I18N_TRANSLATED_LANGUAGES en)
include(cmake/ChunfeQmlTooling.cmake)
chunfe_setup_qml_tooling()
include(GNUInstallDirs)

option(CHUNFE_BUILD_TESTS "Build Chunfe application tests" OFF)
option(CHUNFE_LOG_BUILD_TESTS "Build Chunfe logging tests" ${CHUNFE_BUILD_TESTS})
option(TEXT_BLOCK_BUILD_TESTS "Build TextBlock module tests" ${CHUNFE_BUILD_TESTS})
option(CALENDAR_BUILD_TESTS "Build Calendar module tests" ${CHUNFE_BUILD_TESTS})
if(CHUNFE_BUILD_TESTS OR CHUNFE_LOG_BUILD_TESTS OR TEXT_BLOCK_BUILD_TESTS OR CALENDAR_BUILD_TESTS)
    enable_testing()
endif()
add_subdirectory(3party/miniaudio)
add_subdirectory(modules)
add_subdirectory(plugins/AudioPipline)
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
