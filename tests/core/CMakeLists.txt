# Core layer tests — RenderController facade over the pipeline.
add_ttv_studio_test(
    NAME    test_render_controller
    SOURCES test_render_controller.cpp
    LIBS    ttv_studio_core
)

add_ttv_studio_test(
    NAME    test_settings_store
    SOURCES test_settings_store.cpp
    LIBS    ttv_studio_core
)
