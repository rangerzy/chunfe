find_package(Qt6 6.12 REQUIRED COMPONENTS Gui)

qt_add_library(chunfe_fonts STATIC
    app_fonts.cpp
    app_fonts.h
)
target_compile_features(chunfe_fonts PUBLIC cxx_std_23)
target_link_libraries(chunfe_fonts PUBLIC Qt6::Gui)
target_include_directories(chunfe_fonts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_resources(chunfe_fonts chunfe_font_assets
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        "oppo-sans/OPPO Sans 4.0.ttf"
        "oppo-sans/OPPO Sans 4.0 License Notice.txt"
        "jetbrains-mono/JetBrainsMono-Regular.ttf"
        "jetbrains-mono/JetBrainsMono-SemiBold.ttf"
        "jetbrains-mono/JetBrainsMono-Bold.ttf"
        "jetbrains-mono/OFL.txt"
)
