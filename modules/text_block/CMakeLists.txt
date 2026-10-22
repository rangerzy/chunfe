find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick GLOBAL)
if(NOT DEFINED QT_QML_OUTPUT_DIRECTORY)
    set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")
endif()

qt_add_qml_module(text_block
    URI TextBlock
    VERSION 1.0
    STATIC
    QML_FILES TextBlock.qml
    SOURCES
        text_block_content.h text_block_content.cpp
        paragraph_layout.h paragraph_layout.cpp
        paragraph_item.h paragraph_item.cpp
    DEPENDENCIES QtQuick
)
target_compile_features(text_block PUBLIC cxx_std_23)
set_target_properties(text_block PROPERTIES AUTOMOC_PATH_PREFIX ON)
target_link_libraries(text_block PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
target_include_directories(text_block PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(TEXT_BLOCK_BUILD_TESTS)
    add_subdirectory(tests)
endif()
