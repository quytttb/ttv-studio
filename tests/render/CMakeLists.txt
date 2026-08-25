# Render pipeline core tests — coverage verification, duration optimization
# and scene manifest construction.
add_ttv_studio_test(
    NAME    test_script_coverage
    SOURCES test_script_coverage.cpp
    LIBS    render
)
add_ttv_studio_test(
    NAME    test_duration_optimizer
    SOURCES test_duration_optimizer.cpp
    LIBS    render
)
add_ttv_studio_test(
    NAME    test_scene_manifest
    SOURCES test_scene_manifest.cpp
    LIBS    render
)
add_ttv_studio_test(
    NAME    test_captions
    SOURCES test_captions.cpp
    LIBS    render
)
add_ttv_studio_test(
    NAME    test_scene_planner
    SOURCES test_scene_planner.cpp
    LIBS    render
)
add_ttv_studio_test(
    NAME    test_render_pipeline
    SOURCES test_render_pipeline.cpp
    LIBS    render
)
