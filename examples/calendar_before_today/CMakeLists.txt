cmake_minimum_required(VERSION 3.16)

project(calendar_before_today VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.12)

# Content of downloaded design from Figma to Qt or similar
if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

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


qt_add_executable(appcalendar_before_today
    main.cpp
)

qt_add_qml_module(appcalendar_before_today
    URI calendar_before_today
    QML_FILES
        Main.qml
        PastDateCalendar.qml
        LiquidSelection.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appcalendar_before_today PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appcalendar_before_today
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appcalendar_before_today
    PRIVATE Qt6::Quick
    Qt6::QuickControls2
    chunfe_fonts
)

option(CALENDAR_BUILD_TESTS "Enable calendar interaction tests" OFF)
if(CALENDAR_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS QuickTest)
    enable_testing()
    add_test(NAME calendar_interactions
        COMMAND Qt6::qmltestrunner -input "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    set_tests_properties(calendar_interactions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()

install(TARGETS appcalendar_before_today
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
