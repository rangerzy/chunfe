qt_add_executable(Chunfe main.cpp)
target_compile_features(Chunfe PRIVATE cxx_std_23)
target_link_libraries(Chunfe PRIVATE
    Qt6::Quick Qt6::QuickControls2 chunfe_fonts chunfe_gui chunfe_guiplugin)
# The executable lives beside the QML module; include that module in deployment scans.
set_property(TARGET Chunfe APPEND PROPERTY QT_QML_IMPORT_SCANNER_EXTRA_ROOT_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/../gui")
# Qt 6.12 scans extra roots but only tracks the executable's QML file list for
# rebuilds. Mirror the GUI list so changed imports also refresh deployment data.
get_target_property(chunfe_gui_qml_files chunfe_gui QT_QML_MODULE_QML_FILES)
set_property(TARGET Chunfe APPEND PROPERTY QT_QML_MODULE_QML_FILES ${chunfe_gui_qml_files})
target_compile_definitions(Chunfe PRIVATE
    CHUNFE_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Debug,RelWithDebInfo>:QT_QML_DEBUG>
)
set_target_properties(Chunfe PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.rangerzy.chunfe
    MACOSX_BUNDLE_BUNDLE_NAME Chunfe
    MACOSX_BUNDLE_BUNDLE_VERSION "${CHUNFE_MACOS_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${C_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    FOLDER qtc_runnable
)

install(TARGETS Chunfe
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    qt_generate_deploy_qml_app_script(
        TARGET Chunfe
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
elseif(UNIX)
    # Linux uses the Qt libraries supplied by the KDE Flatpak runtime.
    install(FILES "${CMAKE_BINARY_DIR}/version.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/chunfe)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.rangerzy.chunfe.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.rangerzy.chunfe.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
