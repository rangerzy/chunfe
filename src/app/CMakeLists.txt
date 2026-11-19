find_package(Qt6 6.12 REQUIRED COMPONENTS Multimedia Network Sql)
set(chunfe_model_connection_sources
    deepseek_client.h deepseek_client.cpp
    deepseek_stream.h deepseek_stream.cpp
    model_connection_controller.h model_connection_controller.cpp)
set(chunfe_app_settings_sources app_settings.h app_settings.cpp)
set(chunfe_model_test_chat_sources model_test_chat_controller.h model_test_chat_controller.cpp)
set(chunfe_conversation_store_sources conversation_store.h conversation_store.cpp)
set(chunfe_conversation_advice_text_sources conversation_advice_text.h conversation_advice_text.cpp)
set(chunfe_conversation_advice_sources conversation_advice_controller.h conversation_advice_controller.cpp
    ${chunfe_conversation_advice_text_sources})
set(chunfe_background_sources
    background_controller.h background_controller.cpp
    background_analysis_controller.h background_analysis_controller.cpp)
qt_add_executable(Chunfe
    main.cpp audio_device_list.h audio_device_list.cpp audio_level_test.h audio_level_test.cpp
    offline_transcription_controller.h offline_transcription_controller.cpp
    ${chunfe_model_connection_sources} ${chunfe_app_settings_sources} ${chunfe_background_sources}
    ${chunfe_model_test_chat_sources} ${chunfe_conversation_store_sources} ${chunfe_conversation_advice_sources})
cf_log_configure_target(Chunfe app)
target_compile_features(Chunfe PRIVATE cxx_std_23)
target_link_libraries(Chunfe PRIVATE
    Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Network Qt6::Sql
    chunfe_fonts chunfe_gui chunfe_guiplugin audio_pipline offline_transcription
)

if(CHUNFE_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    qt_add_executable(conversation_store_tests tests/conversation_store_tests.cpp ${chunfe_conversation_store_sources})
    target_compile_features(conversation_store_tests PRIVATE cxx_std_23)
    target_include_directories(conversation_store_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(conversation_store_tests PRIVATE Qt6::Test Qt6::Sql Qt6::Qml)
    add_test(NAME conversation_store_tests COMMAND conversation_store_tests)

    qt_add_executable(conversation_advice_text_tests
        tests/conversation_advice_text_tests.cpp ${chunfe_conversation_advice_text_sources})
    target_compile_features(conversation_advice_text_tests PRIVATE cxx_std_23)
    target_include_directories(conversation_advice_text_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(conversation_advice_text_tests PRIVATE Qt6::Test)
    add_test(NAME conversation_advice_text_tests COMMAND conversation_advice_text_tests)

    qt_add_executable(conversation_advice_controller_tests tests/conversation_advice_controller_tests.cpp
        ${chunfe_conversation_advice_sources} ${chunfe_model_connection_sources} ${chunfe_app_settings_sources})
    target_compile_features(conversation_advice_controller_tests PRIVATE cxx_std_23)
    target_include_directories(conversation_advice_controller_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(conversation_advice_controller_tests PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME conversation_advice_controller_tests COMMAND conversation_advice_controller_tests)

    qt_add_executable(background_controller_tests tests/background_controller_tests.cpp
        ${chunfe_background_sources} ${chunfe_app_settings_sources} ${chunfe_model_connection_sources})
    target_compile_features(background_controller_tests PRIVATE cxx_std_23)
    target_include_directories(background_controller_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(background_controller_tests PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME background_controller_tests COMMAND background_controller_tests)

    qt_add_executable(app_settings_tests tests/app_settings_tests.cpp ${chunfe_app_settings_sources})
    target_compile_features(app_settings_tests PRIVATE cxx_std_23)
    target_include_directories(app_settings_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(app_settings_tests PRIVATE Qt6::Test)
    add_test(NAME app_settings_tests COMMAND app_settings_tests)

    qt_add_executable(model_connection_controller_tests
        tests/model_connection_controller_tests.cpp ${chunfe_model_connection_sources})
    target_compile_features(model_connection_controller_tests PRIVATE cxx_std_23)
    target_include_directories(model_connection_controller_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(model_connection_controller_tests PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME model_connection_controller_tests COMMAND model_connection_controller_tests)

    qt_add_executable(model_test_chat_controller_tests tests/model_test_chat_controller_tests.cpp
        ${chunfe_model_test_chat_sources} ${chunfe_model_connection_sources})
    target_compile_features(model_test_chat_controller_tests PRIVATE cxx_std_23)
    target_include_directories(model_test_chat_controller_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(model_test_chat_controller_tests PRIVATE Qt6::Test Qt6::Network)
    add_test(NAME model_test_chat_controller_tests COMMAND model_test_chat_controller_tests)

    qt_add_executable(application_quit_tests tests/application_quit_tests.cpp)
    target_compile_features(application_quit_tests PRIVATE cxx_std_23)
    target_link_libraries(application_quit_tests PRIVATE Qt6::QuickControls2 chunfe_gui chunfe_guiplugin)
    foreach(state IN ITEMS idle active finished unsaved)
        add_test(NAME application_quit_${state} COMMAND application_quit_tests ${state})
        set_tests_properties(application_quit_${state} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    endforeach()

    qt_add_executable(audio_device_list_tests
        tests/audio_device_list_tests.cpp audio_device_list.h audio_device_list.cpp)
    target_compile_features(audio_device_list_tests PRIVATE cxx_std_23)
    target_include_directories(audio_device_list_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(audio_device_list_tests PRIVATE Qt6::Test Qt6::Multimedia audio_pipline)
    add_test(NAME audio_device_list_tests COMMAND audio_device_list_tests)

    qt_add_executable(audio_level_test_tests
        tests/audio_level_test_tests.cpp audio_level_test.h audio_level_test.cpp
        ../../plugins/AudioPipline/audio_pipline_manager.h)
    target_compile_features(audio_level_test_tests PRIVATE cxx_std_23)
    target_compile_definitions(audio_level_test_tests PRIVATE AUDIO_PIPLINE_LIBRARY)
    target_include_directories(audio_level_test_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/AudioPipline")
    target_link_libraries(audio_level_test_tests PRIVATE Qt6::Test)
    add_test(NAME audio_level_test_tests COMMAND audio_level_test_tests)

    qt_add_executable(offline_transcription_controller_tests
        tests/offline_transcription_controller_tests.cpp offline_transcription_controller.h offline_transcription_controller.cpp)
    target_compile_features(offline_transcription_controller_tests PRIVATE cxx_std_23)
    target_include_directories(offline_transcription_controller_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(offline_transcription_controller_tests PRIVATE Qt6::Test audio_pipline offline_transcription)
    add_test(NAME offline_transcription_controller_tests COMMAND offline_transcription_controller_tests)

    qt_add_executable(offline_transcription_lifecycle_tests
        tests/offline_transcription_lifecycle_tests.cpp offline_transcription_controller.h offline_transcription_controller.cpp)
    target_compile_features(offline_transcription_lifecycle_tests PRIVATE cxx_std_23)
    target_include_directories(offline_transcription_lifecycle_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(offline_transcription_lifecycle_tests PRIVATE Qt6::Test audio_pipline offline_transcription)
    target_compile_definitions(offline_transcription_lifecycle_tests PRIVATE FAKE_RUNTIME_PATH="$<TARGET_FILE:offline_fake_runtime>")
    add_dependencies(offline_transcription_lifecycle_tests offline_fake_runtime)
    chunfe_deploy_offline_runtime(offline_transcription_lifecycle_tests)
    add_test(NAME offline_transcription_lifecycle_tests COMMAND offline_transcription_lifecycle_tests)

    qt_add_executable(offline_transcription_soak_tests
        tests/offline_transcription_soak_tests.cpp offline_transcription_controller.h offline_transcription_controller.cpp)
    target_compile_features(offline_transcription_soak_tests PRIVATE cxx_std_23)
    target_include_directories(offline_transcription_soak_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(offline_transcription_soak_tests PRIVATE Qt6::Test Qt6::Quick audio_pipline offline_transcription)
    chunfe_deploy_offline_runtime(offline_transcription_soak_tests)
    add_test(NAME offline_transcription_soak_tests COMMAND offline_transcription_soak_tests)
    set_tests_properties(offline_transcription_soak_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    if(APPLE)
        add_executable(macos_bundle_localization_tests tests/macos_bundle_localization_tests.mm)
        set_target_properties(macos_bundle_localization_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(macos_bundle_localization_tests PRIVATE "-framework Foundation")
        add_dependencies(macos_bundle_localization_tests Chunfe)
        add_test(NAME macos_bundle_localization_tests
            COMMAND macos_bundle_localization_tests "$<TARGET_BUNDLE_DIR:Chunfe>")
    endif()
endif()
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
chunfe_deploy_offline_runtime(Chunfe)

install(TARGETS Chunfe
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(APPLE)
    set(chunfe_macos_icon "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/branding/chunfe-app.icns")
    set_source_files_properties("${chunfe_macos_icon}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(
        "localizations/zh-Hans.lproj/InfoPlist.strings"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/zh-Hans.lproj")
    set_source_files_properties(
        "localizations/en.lproj/InfoPlist.strings"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/en.lproj")
    target_sources(Chunfe PRIVATE
        "${chunfe_macos_icon}"
        "localizations/zh-Hans.lproj/InfoPlist.strings"
        "localizations/en.lproj/InfoPlist.strings")
    find_package(Qt6 6.12 REQUIRED COMPONENTS Svg)
    if(NOT TARGET Qt6::QSvgPlugin)
        message(FATAL_ERROR "Qt SVG image format plugin is required for application icons")
    endif()
    if(NOT TARGET Qt6::QSQLiteDriverPlugin)
        message(FATAL_ERROR "Qt SQLite driver plugin is required for conversation history")
    endif()
    set_target_properties(Chunfe PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "chunfe-app.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
    # Refresh the bundle timestamp so macOS reloads its cached icon after a rebuild.
    set_property(TARGET Chunfe APPEND PROPERTY LINK_DEPENDS "${chunfe_macos_icon}")
    add_custom_command(TARGET Chunfe POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E touch "$<TARGET_BUNDLE_DIR:Chunfe>"
        VERBATIM)
    # Keep the project's shared library inside the application bundle.  Install
    # it before Qt's deploy script so macdeployqt can resolve its dependencies.
    set_target_properties(audio_pipline offline_transcription PROPERTIES INSTALL_NAME_DIR "@rpath")
    set_property(TARGET Chunfe APPEND PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
    install(TARGETS audio_pipline offline_transcription
        LIBRARY DESTINATION "Chunfe.app/Contents/Frameworks")
    chunfe_install_offline_runtime("Chunfe.app/Contents/Frameworks"
        "Chunfe.app/Contents/Resources/licenses/audio.cpp"
        "Chunfe.app/Contents/Resources/offline-transcription")
    # Install the drivers before macdeployqt so it can fix their framework references.
    install(PROGRAMS "$<TARGET_FILE:Qt6::QSvgPlugin>"
        DESTINATION "Chunfe.app/Contents/PlugIns/imageformats")
    install(PROGRAMS "$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>"
        DESTINATION "Chunfe.app/Contents/PlugIns/sqldrivers")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/qt-macos.conf"
        DESTINATION "Chunfe.app/Contents/Resources" RENAME qt.conf)
    qt_generate_deploy_qml_app_script(
        TARGET Chunfe
        OUTPUT_SCRIPT deploy_script
        DEPLOY_TOOL_OPTIONS
            "-executable=Chunfe.app/Contents/PlugIns/imageformats/$<TARGET_FILE_NAME:Qt6::QSvgPlugin>"
            "-executable=Chunfe.app/Contents/PlugIns/sqldrivers/$<TARGET_FILE_NAME:Qt6::QSQLiteDriverPlugin>"
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
elseif(WIN32)
    enable_language(RC)
    get_filename_component(CHUNFE_APP_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/branding/chunfe-app.ico" ABSOLUTE)
    set(chunfe_windows_resources "${CMAKE_CURRENT_BINARY_DIR}/chunfe.rc")
    configure_file("../../packaging/windows/chunfe.rc.in"
        "${chunfe_windows_resources}" @ONLY)
    set_source_files_properties("${chunfe_windows_resources}"
        PROPERTIES OBJECT_DEPENDS "${CHUNFE_APP_ICON}")
    target_sources(Chunfe PRIVATE "${chunfe_windows_resources}")
    install(TARGETS audio_pipline offline_transcription RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    chunfe_install_offline_runtime("${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_BINDIR}/licenses/audio.cpp" "${CMAKE_INSTALL_BINDIR}")
elseif(UNIX)
    set_property(TARGET Chunfe APPEND PROPERTY INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    install(TARGETS audio_pipline offline_transcription LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    chunfe_install_offline_runtime("${CMAKE_INSTALL_LIBDIR}/chunfe"
        "${CMAKE_INSTALL_DATADIR}/licenses/audio.cpp" "${CMAKE_INSTALL_LIBDIR}/chunfe")
    # Linux uses the Qt libraries supplied by the KDE Flatpak runtime.
    install(FILES "${CMAKE_BINARY_DIR}/version.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/chunfe)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.rangerzy.chunfe.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/io.github.rangerzy.chunfe.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
