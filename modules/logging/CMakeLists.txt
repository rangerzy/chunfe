cmake_minimum_required(VERSION 4.3.6)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(chunfe_log LANGUAGES CXX)
    enable_testing()
endif()

find_package(Qt6 6.12 REQUIRED COMPONENTS Core)

add_library(chunfe_log STATIC cf_log.cpp cf_log.h)
set_target_properties(chunfe_log PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(chunfe_log PUBLIC Qt6::Core)
target_include_directories(chunfe_log PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Configure the code that emits messages, not its dependencies or consumers.
# By default the category is the CMake target name; pass a module name if needed.
function(cf_log_configure_target target)
    if(ARGC GREATER 1)
        set(category "${ARGV1}")
    else()
        set(category "${target}")
    endif()
    target_link_libraries(${target} PRIVATE chunfe_log)
    target_compile_definitions(${target} PRIVATE
        QT_MESSAGELOGCONTEXT "CF_LOG_CATEGORY=\"${category}\"")
endfunction()

option(CHUNFE_LOG_BUILD_TESTS "Build Chunfe logging tests" ${CHUNFE_BUILD_TESTS})
if(CHUNFE_LOG_BUILD_TESTS)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    set(CMAKE_AUTOMOC ON)
    add_library(chunfe_log_test_alpha STATIC tests/target_log.cpp)
    cf_log_configure_target(chunfe_log_test_alpha)
    target_compile_definitions(chunfe_log_test_alpha PRIVATE CF_LOG_TEST_FUNCTION=emitAlpha)
    add_library(chunfe_log_test_beta STATIC tests/target_log.cpp)
    cf_log_configure_target(chunfe_log_test_beta beta)
    target_compile_definitions(chunfe_log_test_beta PRIVATE CF_LOG_TEST_FUNCTION=emitBeta)
    add_executable(chunfe_log_probe tests/log_probe.cpp)
    cf_log_configure_target(chunfe_log_probe logging_test)
    target_link_libraries(chunfe_log_probe PRIVATE chunfe_log_test_alpha chunfe_log_test_beta)
    add_executable(chunfe_log_tests tests/logging_tests.cpp)
    target_link_libraries(chunfe_log_tests PRIVATE Qt6::Test)
    target_compile_definitions(chunfe_log_tests PRIVATE
        "CF_LOG_PROBE_PATH=\"$<TARGET_FILE:chunfe_log_probe>\"")
    add_dependencies(chunfe_log_tests chunfe_log_probe)
    add_test(NAME chunfe_log_tests COMMAND chunfe_log_tests)
endif()
