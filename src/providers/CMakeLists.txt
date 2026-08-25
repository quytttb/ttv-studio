# Provider layer — REST adapters for the external services of the redub
# pipeline (LLM / TTS / video gateway). Error classification (Transient /
# Permanent / AmbiguousTimeout) + retry live here; no pipeline logic.

qt_add_library(providers STATIC
    ProviderError.h
    ProviderError.cpp
    Retry.h
    Retry.cpp
    Transport.h
    QNamTransport.h
    QNamTransport.cpp
    LlmClient.h
    LlmClient.cpp
    TtsClient.h
    TtsClient.cpp
    VideoGatewayClient.h
    VideoGatewayClient.cpp
)

target_include_directories(providers
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(providers
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        ttv_studio::media   # Ffprobe audio validation in TtsClient
        ttv_studio::utils   # AppConstants defaults
)

add_library(ttv_studio::providers ALIAS providers)
