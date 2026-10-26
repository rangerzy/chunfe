find_package(Qt6 6.12 REQUIRED COMPONENTS QuickTest QuickControls2)

qt_add_executable(calendar_tests calendar_tests.cpp)
target_link_libraries(calendar_tests PRIVATE
    Qt6::QuickTest Qt6::QuickControls2 calendar calendarplugin)

add_test(NAME calendar_tests
    COMMAND calendar_tests -input "${CMAKE_CURRENT_SOURCE_DIR}")
set_tests_properties(calendar_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
