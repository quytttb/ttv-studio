# Provider layer tests — error classification, retry policy and the REST
# client adapters (LLM / TTS / video gateway) against a scripted transport.
add_ttv_studio_test(
    NAME    test_provider_error
    SOURCES test_provider_error.cpp
    LIBS    providers
)
add_ttv_studio_test(
    NAME    test_retry
    SOURCES test_retry.cpp
    LIBS    providers
)
add_ttv_studio_test(
    NAME    test_llm_client
    SOURCES test_llm_client.cpp
    LIBS    providers
)
add_ttv_studio_test(
    NAME    test_tts_client
    SOURCES test_tts_client.cpp
    LIBS    providers media
)
add_ttv_studio_test(
    NAME    test_video_gateway
    SOURCES test_video_gateway.cpp
    LIBS    providers
)
add_ttv_studio_test(
    NAME    test_qnam_transport
    SOURCES test_qnam_transport.cpp
    LIBS    providers
)
add_ttv_studio_test(
    NAME    test_github_releases_client
    SOURCES test_github_releases_client.cpp
    LIBS    providers utils
)
