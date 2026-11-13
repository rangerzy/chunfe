find_package(Qt6 6.12 REQUIRED COMPONENTS Test)

qt_add_executable(audio_pipline_manager_tests
    audio_pipline_manager_tests.cpp
    ../../../src/app/audio_level_test.cpp
    ../../../src/app/audio_level_test.h
    ../audio_pipline_manager.cpp
    ../audio_pipline_manager.h
    ../captures/audio_capture_worker.cpp
    ../captures/audio_capture_worker.h
    ../captures/audio_capture_context.cpp
    ../captures/audio_capture_context.h
)
set_target_properties(audio_pipline_manager_tests PROPERTIES AUTOMOC ON)
target_include_directories(audio_pipline_manager_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/app"
    "$<TARGET_PROPERTY:miniaudio,INTERFACE_INCLUDE_DIRECTORIES>"
)
target_compile_definitions(audio_pipline_manager_tests PRIVATE AUDIO_PIPLINE_LIBRARY)
target_link_libraries(audio_pipline_manager_tests PRIVATE Qt6::Test)
if(APPLE)
    target_sources(audio_pipline_manager_tests PRIVATE fake_mac_system_capture.cpp)
endif()
cf_log_configure_target(audio_pipline_manager_tests audio_pipline)
add_test(NAME audio_pipline_manager_tests COMMAND audio_pipline_manager_tests)

qt_add_executable(audio_capture_buffer_tests audio_capture_buffer_tests.cpp)
target_link_libraries(audio_capture_buffer_tests PRIVATE Qt6::Test)
add_test(NAME audio_capture_buffer_tests COMMAND audio_capture_buffer_tests)

if(APPLE)
    add_executable(miniaudio_coreaudio_enumeration_tests miniaudio_coreaudio_enumeration_tests.c)
    target_include_directories(miniaudio_coreaudio_enumeration_tests PRIVATE
        "$<TARGET_PROPERTY:miniaudio,INTERFACE_INCLUDE_DIRECTORIES>")
    target_link_libraries(miniaudio_coreaudio_enumeration_tests PRIVATE
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreFoundation")
    add_test(NAME miniaudio_coreaudio_enumeration_tests COMMAND miniaudio_coreaudio_enumeration_tests)

    qt_add_executable(mac_system_capture_tests mac_system_capture_tests.mm)
    set_target_properties(mac_system_capture_tests PROPERTIES OBJCXX_STANDARD 17)
    target_compile_options(mac_system_capture_tests PRIVATE -fobjc-arc)
    target_link_libraries(mac_system_capture_tests PRIVATE Qt6::Core miniaudio
        "-framework CoreAudio" "-framework AudioToolbox" "-framework AVFoundation" "-framework Foundation")
    add_test(NAME mac_system_capture_tests COMMAND mac_system_capture_tests)
endif()
