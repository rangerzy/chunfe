find_package(Qt6 6.12 REQUIRED COMPONENTS Gui)

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

qt_add_library(chunfe_fonts STATIC
    app_fonts.cpp
    app_fonts.h
)
target_compile_features(chunfe_fonts PUBLIC cxx_std_23)
target_link_libraries(chunfe_fonts PUBLIC Qt6::Gui)
cf_log_configure_target(chunfe_fonts fonts)
target_include_directories(chunfe_fonts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_resources(chunfe_fonts chunfe_font_assets
    BIG_RESOURCES
    PREFIX "/fonts"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        "oppo-sans/OPPO Sans 4.0.ttf"
        "oppo-sans/OPPO Sans 4.0 License Notice.txt"
        "Inter-4.1/extras/ttf/Inter-Black.ttf"
        "Inter-4.1/extras/ttf/Inter-BlackItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-Bold.ttf"
        "Inter-4.1/extras/ttf/Inter-BoldItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-ExtraBold.ttf"
        "Inter-4.1/extras/ttf/Inter-ExtraBoldItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-ExtraLight.ttf"
        "Inter-4.1/extras/ttf/Inter-ExtraLightItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-Italic.ttf"
        "Inter-4.1/extras/ttf/Inter-Light.ttf"
        "Inter-4.1/extras/ttf/Inter-LightItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-Medium.ttf"
        "Inter-4.1/extras/ttf/Inter-MediumItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-Regular.ttf"
        "Inter-4.1/extras/ttf/Inter-SemiBold.ttf"
        "Inter-4.1/extras/ttf/Inter-SemiBoldItalic.ttf"
        "Inter-4.1/extras/ttf/Inter-Thin.ttf"
        "Inter-4.1/extras/ttf/Inter-ThinItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Black.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-BlackItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Bold.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-BoldItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-ExtraBold.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-ExtraBoldItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-ExtraLight.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-ExtraLightItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Italic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Light.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-LightItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Medium.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-MediumItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Regular.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-SemiBold.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-SemiBoldItalic.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-Thin.ttf"
        "Inter-4.1/extras/ttf/InterDisplay-ThinItalic.ttf"
        "Inter-4.1/LICENSE.txt"
)
