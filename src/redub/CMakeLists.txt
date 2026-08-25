# Redub pipeline core — transcript/translation domain and the duration-aware
# dubbing translator. Ingest (yt-dlp), STT and assembly land in P4b.

qt_add_library(redub STATIC
    Transcript.h
    Transcript.cpp
    Translator.h
    Translator.cpp
    DubPlanner.h
    DubPlanner.cpp
)

target_include_directories(redub
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(redub
    PUBLIC
        Qt6::Core
        ttv_studio::providers   # LlmClient for the translator
        ttv_studio::utils       # AppConstants dubbing defaults
)

add_library(ttv_studio::redub ALIAS redub)
