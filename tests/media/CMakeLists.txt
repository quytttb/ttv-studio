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
