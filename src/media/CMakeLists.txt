# Media layer — typed subprocess wrappers around external binaries
# (ffmpeg / ffprobe / later yt-dlp). No business logic.

qt_add_library(media STATIC
    Subprocess.h
    Subprocess.cpp
    Ffprobe.h
    Ffprobe.cpp
    HardwareEncoder.h
    HardwareEncoder.cpp
    MediaEngine.h
    MediaEngine.cpp
    YtDlp.h
    YtDlp.cpp
    WhisperStt.h
    WhisperStt.cpp
)

target_include_directories(media
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(media
    PUBLIC
        Qt6::Core
    PRIVATE
        ttv_studio::utils
)

add_library(ttv_studio::media ALIAS media)
