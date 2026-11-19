# audio.cpp is an application asset. This build-time directory is never exposed
# as a setting in the application; only the GGUF model is selected by the user.
if(APPLE)
    set(_offline_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_OSX_ARCHITECTURES)
        set(_offline_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    set(_offline_platform "macos/${_offline_arch}")
    set(_offline_entry "libaudiocpp.dylib")
    set(_offline_glob "*.dylib")
elseif(WIN32)
    set(_offline_platform "windows/x64")
    set(_offline_entry "audiocpp.dll")
    set(_offline_glob "*.dll")
else()
    set(_offline_platform "linux/${CMAKE_SYSTEM_PROCESSOR}")
    set(_offline_entry "libaudiocpp.so")
    set(_offline_glob "*.so*")
    file(RELATIVE_PATH _offline_install_relative
        "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}/chunfe/libaudiocpp.so")
    target_compile_definitions(offline_transcription PUBLIC
        CHUNFE_AUDIOCPP_INSTALL_RELATIVE_PATH="${_offline_install_relative}")
endif()
set(CHUNFE_AUDIOCPP_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/lib/${_offline_platform}" CACHE PATH
    "Build-time directory containing the GPU audio.cpp runtime and its redistributable dependencies")
option(CHUNFE_REQUIRE_AUDIOCPP_RUNTIME
    "Reject installation when the GPU audio.cpp runtime package is missing" ${APPLE})

set(_offline_runtime_files)
set(_offline_runtime_names)
# Accept the reference project's Vulkan artifact name, but deploy one GPU-only
# entry point under the canonical name used by the application.
set(_offline_source_entry "${_offline_entry}")
if(WIN32 AND EXISTS "${CHUNFE_AUDIOCPP_RUNTIME_DIR}/audiocpp-vulkan.dll")
    set(_offline_source_entry "audiocpp-vulkan.dll")
endif()
if(EXISTS "${CHUNFE_AUDIOCPP_RUNTIME_DIR}/${_offline_source_entry}")
    list(APPEND _offline_runtime_files "${CHUNFE_AUDIOCPP_RUNTIME_DIR}/${_offline_source_entry}")
    list(APPEND _offline_runtime_names "${_offline_entry}")
    file(GLOB _offline_dependencies CONFIGURE_DEPENDS
        "${CHUNFE_AUDIOCPP_RUNTIME_DIR}/${_offline_glob}")
    foreach(_dependency IN LISTS _offline_dependencies)
        get_filename_component(_name "${_dependency}" NAME)
        if(_name STREQUAL _offline_source_entry OR _name STREQUAL _offline_entry)
            continue()
        endif()
        list(APPEND _offline_runtime_files "${_dependency}")
        list(APPEND _offline_runtime_names "${_name}")
    endforeach()
else()
    message(WARNING "GPU audio.cpp runtime is missing from ${CHUNFE_AUDIOCPP_RUNTIME_DIR}; offline transcription is unavailable. Set CHUNFE_AUDIOCPP_RUNTIME_DIR to a verified GPU runtime package. CHUNFE_REQUIRE_AUDIOCPP_RUNTIME=ON rejects installation without it.")
endif()
set_target_properties(offline_transcription PROPERTIES
    AUDIOCPP_RUNTIME_FILES "${_offline_runtime_files}"
    AUDIOCPP_RUNTIME_NAMES "${_offline_runtime_names}"
    AUDIOCPP_VAD_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/vendor/audio.cpp/models/silero_vad_16k.safetensors"
    AUDIOCPP_NOTICE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/audio.cpp")

function(chunfe_deploy_offline_runtime target)
    get_target_property(_files offline_transcription AUDIOCPP_RUNTIME_FILES)
    get_target_property(_names offline_transcription AUDIOCPP_RUNTIME_NAMES)
    get_target_property(_vad_model offline_transcription AUDIOCPP_VAD_MODEL)
    if(NOT _files)
        return()
    endif()
    get_target_property(_bundle ${target} MACOSX_BUNDLE)
    if(APPLE AND _bundle)
        set(_destination "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks")
        set(_vad_destination "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/offline-transcription")
    else()
        set(_destination "$<TARGET_FILE_DIR:${target}>")
        set(_vad_destination "${_destination}")
    endif()
    set(_commands COMMAND "${CMAKE_COMMAND}" -E make_directory "${_destination}")
    foreach(_file _name IN ZIP_LISTS _files _names)
        list(APPEND _commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_file}" "${_destination}/${_name}")
    endforeach()
    list(APPEND _commands COMMAND "${CMAKE_COMMAND}" -E make_directory "${_vad_destination}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_vad_model}" "${_vad_destination}")
    # Run even on no-op builds so deleting a deployed runtime is recoverable.
    add_custom_target(${target}_offline_runtime ${_commands}
        COMMENT "Bundling GPU audio.cpp runtime for ${target}" VERBATIM)
    add_dependencies(${target} ${target}_offline_runtime)
endfunction()

function(chunfe_install_offline_runtime runtime_destination notice_destination vad_destination)
    get_target_property(_files offline_transcription AUDIOCPP_RUNTIME_FILES)
    get_target_property(_names offline_transcription AUDIOCPP_RUNTIME_NAMES)
    if(NOT _files)
        if(CHUNFE_REQUIRE_AUDIOCPP_RUNTIME)
            install(CODE "message(FATAL_ERROR \"Cannot package Chunfe without its GPU audio.cpp runtime; offline transcription is unavailable. Supply CHUNFE_AUDIOCPP_RUNTIME_DIR and rebuild.\")")
        else()
            install(CODE "message(WARNING \"Installing Chunfe without a GPU audio.cpp runtime; offline transcription is unavailable in this package. Supply CHUNFE_AUDIOCPP_RUNTIME_DIR and rebuild to enable it.\")")
        endif()
        return()
    endif()
    foreach(_file _name IN ZIP_LISTS _files _names)
        install(FILES "${_file}" DESTINATION "${runtime_destination}" RENAME "${_name}")
    endforeach()
    get_target_property(_vad_model offline_transcription AUDIOCPP_VAD_MODEL)
    install(FILES "${_vad_model}" DESTINATION "${vad_destination}")
    get_target_property(_notice_dir offline_transcription AUDIOCPP_NOTICE_DIR)
    install(FILES "${_notice_dir}/LICENSE" "${_notice_dir}/ThirdPartyNotices.md"
        DESTINATION "${notice_destination}")
    install(DIRECTORY "${_notice_dir}/licenses" DESTINATION "${notice_destination}")
endfunction()
