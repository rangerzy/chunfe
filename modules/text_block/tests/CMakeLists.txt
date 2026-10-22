find_package(Qt6 6.12 REQUIRED COMPONENTS Test Quick Qml)
qt_add_executable(text_block_tests text_block_tests.cpp)
target_link_libraries(text_block_tests PRIVATE Qt6::Test text_block text_blockplugin)
qt_add_executable(text_block_layout_tests layout_tests.cpp)
target_link_libraries(text_block_layout_tests PRIVATE Qt6::Test text_block)
add_test(NAME text_block_tests COMMAND text_block_tests)
add_test(NAME text_block_layout_tests COMMAND text_block_layout_tests)
set_tests_properties(text_block_tests text_block_layout_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
