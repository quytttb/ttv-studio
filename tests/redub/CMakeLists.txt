# Redub core tests — transcript artifacts and the duration-aware translator.
add_ttv_studio_test(
    NAME    test_transcript
    SOURCES test_transcript.cpp
    LIBS    redub
)
add_ttv_studio_test(
    NAME    test_translator
    SOURCES test_translator.cpp
    LIBS    redub
)
add_ttv_studio_test(
    NAME    test_dub_planner
    SOURCES test_dub_planner.cpp
    LIBS    redub
)

add_ttv_studio_test(
    NAME    test_redub_pipeline
    SOURCES test_redub_pipeline.cpp
    LIBS    redub
)
