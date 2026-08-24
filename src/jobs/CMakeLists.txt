# Jobs layer — pipeline state machine + durable job registry.
# Provider-agnostic: no vendor SDKs, no model IDs, no HTTP here.

qt_add_library(jobs STATIC
    JobTypes.h
    JobTypes.cpp
    JobRecord.h
    JobRecord.cpp
    JobStore.h
    JobStore.cpp
)

target_include_directories(jobs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(jobs
    PUBLIC
        Qt6::Core
        ttv_studio::utils
)

add_library(ttv_studio::jobs ALIAS jobs)
