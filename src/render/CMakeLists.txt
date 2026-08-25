# Render pipeline core — deterministic planning/timing logic:
# script coverage, duration optimization, scene manifest. No QObject, no
# network; the orchestrator (P3b) wires this to providers + JobStore.

qt_add_library(render STATIC
    SceneTypes.h
    SceneTypes.cpp
    ScriptCoverage.h
    ScriptCoverage.cpp
    DurationOptimizer.h
    DurationOptimizer.cpp
    SceneManifest.h
    SceneManifest.cpp
)

target_include_directories(render
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(render
    PUBLIC
        Qt6::Core
    PRIVATE
        ttv_studio::utils   # AppConstants render defaults
)

add_library(ttv_studio::render ALIAS render)
