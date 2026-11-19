cmake_minimum_required(VERSION 4.3.6)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(miniaudio_lib LANGUAGES C)
else()
    enable_language(C)
endif()

add_library(miniaudio STATIC miniaudio.c)
set_target_properties(miniaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(miniaudio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(APPLE)
    find_library(COREAUDIO_LIB CoreAudio REQUIRED)
    find_library(AUDIOUNIT_LIB AudioUnit REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    target_link_libraries(miniaudio PRIVATE
        ${COREAUDIO_LIB}
        ${AUDIOUNIT_LIB}
        ${COREFOUNDATION_LIB}
    )
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PRIVATE
        Threads::Threads
        m
        ${CMAKE_DL_LIBS}
    )
endif()
