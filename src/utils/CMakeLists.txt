# Utils layer — stateless helpers shared by upper layers.
# Rule: no QObject, no QML, no business logic; linkable from any upper layer.

qt_add_library(utils STATIC
    AppConstants.h
    Paths.h
    Paths.cpp
    Version.h
    Version.h.in
    UiConstants.h
    FormatConstants.h
    time/DateTimeUtils.h
    time/DateTimeUtils.cpp
    os/WindowsFramelessHelper.h
    os/WindowsFramelessHelper.cpp
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_BINARY_DIR}/generated       # for Version_generated.h (configure_file output)
)

target_link_libraries(utils
    PUBLIC
        Qt6::Core
)

add_library(ttv_studio::utils ALIAS utils)
