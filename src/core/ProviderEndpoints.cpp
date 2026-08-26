#include "ProviderEndpoints.h"

#include "SettingsStore.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Core {

ProviderEndpoints ProviderEndpoints::fromEnvironment()
{
    // Resolution contract lives in SettingsStore::resolvedValue:
    // env var → stored setting → built-in default.

    ProviderEndpoints e;
    e.llmBaseUrl = SettingsStore::resolvedValue(
        "TTV_LLM_BASE_URL", QStringLiteral("llm_base_url"),
        QLatin1String(Defaults::kDefaultLlmBaseUrl));
    e.llmApiKey = SettingsStore::resolvedValue("TTV_LLM_API_KEY",
                                               QStringLiteral("llm_api_key"), {});
    e.llmModel = SettingsStore::resolvedValue("TTV_LLM_MODEL",
                                              QStringLiteral("llm_model"), {});
    e.ttsBaseUrl = SettingsStore::resolvedValue(
        "TTV_TTS_BASE_URL", QStringLiteral("tts_base_url"),
        QLatin1String(Defaults::kDefaultTtsBaseUrl));
    e.videoGatewayBaseUrl = SettingsStore::resolvedValue(
        "TTV_VIDEO_GATEWAY_BASE_URL", QStringLiteral("video_gateway_base_url"),
        QLatin1String(Defaults::kDefaultVideoGatewayBaseUrl));
    e.videoGatewayApiKey =
        SettingsStore::resolvedValue("TTV_VIDEO_GATEWAY_API_KEY",
                                     QStringLiteral("video_gateway_api_key"), {});
    e.videoModel = SettingsStore::resolvedValue("TTV_VIDEO_MODEL",
                                                QStringLiteral("video_model"), {});
    return e;
}

} // namespace TtvStudio::Core
