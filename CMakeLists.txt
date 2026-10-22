cmake_minimum_required(VERSION 3.24)

project(Chunfe VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(Chunfe
    main.cpp
)

qt_add_qml_module(Chunfe
    URI Chunfe
    VERSION 1.0
    QML_FILES Main.qml
    QML_FILES Test.qml
)

target_link_libraries(Chunfe PRIVATE Qt6::Quick)
target_compile_definitions(Chunfe PRIVATE
    $<$<CONFIG:Debug,RelWithDebInfo>:QT_QML_DEBUG>
)

set_target_properties(Chunfe PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "春风"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.rangerzy.chunfe"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE TRUE
)

file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/version.txt" CONTENT "${PROJECT_VERSION}\n")

include(GNUInstallDirs)
install(TARGETS Chunfe
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(APPLE)
    qt_generate_deploy_qml_app_script(
        TARGET Chunfe
        OUTPUT_SCRIPT chunfe_deploy_script
        DEPLOY_TOOL_OPTIONS -no-codesign -verbose=1
    )
    install(SCRIPT "${chunfe_deploy_script}")
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/io.github.rangerzy.chunfe.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES packaging/linux/io.github.rangerzy.chunfe.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
endif()
