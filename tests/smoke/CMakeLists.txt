# Smoke tests for the stripped-down shell — grows alongside the pipeline layers.
add_ttv_studio_test(
    NAME    test_version_smoke
    SOURCES test_version_smoke.cpp
    LIBS    utils
)

# The smoke test asserts the generated Version.h matches project(VERSION);
# inject the expected values instead of hardcoding them so release bumps
# (the workflow rewrites VERSION from the git tag) never break the test.
target_compile_definitions(test_version_smoke PRIVATE
    kProjectVersionMajor=${PROJECT_VERSION_MAJOR}
    kProjectVersionMinor=${PROJECT_VERSION_MINOR}
    kProjectVersionPatch=${PROJECT_VERSION_PATCH}
)
