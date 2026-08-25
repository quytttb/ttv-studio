# Media layer tests — subprocess runner and ffprobe parsing.
add_ttv_studio_test(
    NAME    test_subprocess
    SOURCES test_subprocess.cpp
    LIBS    media
)
add_ttv_studio_test(
    NAME    test_ffprobe
    SOURCES test_ffprobe.cpp
    LIBS    media
)
add_ttv_studio_test(
    NAME    test_hardware_encoder
    SOURCES test_hardware_encoder.cpp
    LIBS    media
)
add_ttv_studio_test(
    NAME    test_media_engine
    SOURCES test_media_engine.cpp
    LIBS    media
)

add_ttv_studio_test(
    NAME    test_ytdlp
    SOURCES test_ytdlp.cpp
    LIBS    media
)
