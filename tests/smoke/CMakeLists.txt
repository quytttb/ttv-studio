# Smoke tests for the stripped-down shell — grows alongside the pipeline layers.
add_ttv_studio_test(
    NAME    test_version_smoke
    SOURCES test_version_smoke.cpp
    LIBS    utils
)
