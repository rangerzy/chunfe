cmake_minimum_required(VERSION 4.3.6)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(audio_pipline LANGUAGES C CXX)
    add_subdirectory("../../3party/miniaudio" "${CMAKE_CURRENT_BINARY_DIR}/miniaudio")
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core)

if(NOT TARGET chunfe_log)
    get_filename_component(_default_log_source
        "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/logging" ABSOLUTE)
    set(CHUNFE_LOG_SOURCE_DIR "${_default_log_source}" CACHE PATH
        "Path to the Chunfe logging module source directory")
    if(NOT EXISTS "${CHUNFE_LOG_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Set CHUNFE_LOG_SOURCE_DIR to the path of modules/logging")
    endif()
    add_subdirectory("${CHUNFE_LOG_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/logging")
endif()

add_library(audio_pipline SHARED
    audio_pipline_manager.h audio_pipline_manager.cpp
    audio_pipline_types.h captures/audio_capture_buffer.h
    captures/audio_capture_worker.h captures/audio_capture_worker.cpp
    captures/audio_capture_context.h captures/audio_capture_context.cpp
)
cf_log_configure_target(audio_pipline)
target_compile_definitions(audio_pipline PRIVATE AUDIO_PIPLINE_LIBRARY)

target_include_directories(audio_pipline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(audio_pipline
    PUBLIC
        Qt6::Core
    PRIVATE
        miniaudio
)

if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(captures/mac_system_capture.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_sources(audio_pipline PRIVATE captures/mac_system_capture.h captures/mac_system_capture.mm)
    target_link_libraries(audio_pipline PRIVATE
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AVFoundation" "-framework Foundation")
endif()

if(CHUNFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
