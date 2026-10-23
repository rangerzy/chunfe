include_guard(GLOBAL)

function(chunfe_track_qmlls_metadata)
    # Qt 6.12 omits this configure-time fragment from the generated build rule.
    set(metadata "${CMAKE_BINARY_DIR}/.qt/.qmlls.build.ini")
    if(TARGET generate_qmlls_build_ini_file AND EXISTS "${metadata}.part")
        add_custom_command(OUTPUT "${metadata}" APPEND DEPENDS "${metadata}.part")
    endif()
endfunction()

macro(chunfe_setup_qml_tooling)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
    # Qt Creator's classic code model reads this separately from qmlls.
    list(PREPEND QML_IMPORT_PATH "${QT_QML_OUTPUT_DIRECTORY}")
    list(REMOVE_DUPLICATES QML_IMPORT_PATH)
    set(QML_IMPORT_PATH "${QML_IMPORT_PATH}" CACHE STRING "QML import paths for Qt Creator" FORCE)
    cmake_language(DEFER CALL chunfe_track_qmlls_metadata)
endmacro()
