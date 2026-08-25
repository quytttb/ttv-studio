# Render pipeline core — deterministic planning/timing logic plus the
# blocking orchestrator that drives the P1 state chart through the provider
# clients. Network/subprocess seams stay injectable for tests.

qt_add_library(render STATIC
    SceneTypes.h
    SceneTypes.cpp
    ScriptCoverage.h
    ScriptCoverage.cpp
    DurationOptimizer.h
    DurationOptimizer.cpp
    SceneManifest.h
    SceneManifest.cpp
    Captions.h
    Captions.cpp
    ScenePlanner.h
    ScenePlanner.cpp
    RenderPipeline.h
    RenderPipeline.cpp
)

target_include_directories(render
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(render
    PUBLIC
        Qt6::Core
        ttv_studio::jobs       # state chart + durable records
        ttv_studio::providers  # TTS / LLM / video gateway clients
        ttv_studio::media      # Ffprobe / MediaEngine
    PRIVATE
        ttv_studio::utils      # AppConstants defaults
)

add_library(ttv_studio::render ALIAS render)
