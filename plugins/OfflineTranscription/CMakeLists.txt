add_library(offline_transcription SHARED
    offline_transcription_engine.cpp
    offline_transcription_engine.h
    offline_transcription_text.h
    offline_voice_activity.cpp
    offline_voice_activity.h)
target_compile_features(offline_transcription PUBLIC cxx_std_23)
target_compile_definitions(offline_transcription PRIVATE OFFLINE_TRANSCRIPTION_LIBRARY)
target_include_directories(offline_transcription PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(offline_transcription PUBLIC Qt6::Core)
include(cmake/AudioCppRuntime.cmake)
if(CHUNFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
