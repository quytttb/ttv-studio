# Core domain — C++ types exposed to QML (module URI TtvStudio.Core):
# job list model + render controller facade over the pipeline.

qt_add_qml_module(ttv_studio_core
    URI TtvStudio.Core
    VERSION ${QML_MODULE_VERSION}
    STATIC
    SOURCES
        ProviderEndpoints.h
        ProviderEndpoints.cpp
        JobListModel.h
        JobListModel.cpp
        RenderController.h
        RenderController.cpp
        RenderDeviceController.h
        RenderDeviceController.cpp
        SettingsStore.h
        SettingsStore.cpp
    NO_PLUGIN_OPTIONAL
)

target_include_directories(ttv_studio_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(ttv_studio_core
    PUBLIC
        Qt6::Quick
        ttv_studio::jobs
        ttv_studio::render
        ttv_studio::redub
    PRIVATE
        ttv_studio::providers
        ttv_studio::media
        ttv_studio::utils
)

add_library(ttv_studio::core ALIAS ttv_studio_core)
