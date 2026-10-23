qt_add_qml_module(chunfe_gui
    URI Chunfe
    STATIC
    CLASS_NAME ChunfePlugin
    QML_FILES Main.qml
)
target_compile_features(chunfe_gui PRIVATE cxx_std_23)
target_link_libraries(chunfe_gui PRIVATE Qt6::Quick Qt6::QuickControls2)
