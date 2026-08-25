#include "ProviderEndpoints.h"

#include <QtGlobal>

#include "SettingsStore.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Core {

namespace {

// Resolution order per contract: env var → stored setting → built-in default.
QString resolve(const char *envName, const QString &settingKey, const QString &fallback)
{
    const QString fromEnv = qEnvironmentVariable(envName);
    if (!fromEnv.isEmpty())
        return fromEnv;
    const QString stored = SettingsStore::storedValue(settingKey);
    if (!stored.isEmpty())
        return stored;
    return fallback;
}


} // namespace

ProviderEndpoints ProviderEndpoints::fromEnvironment()
{
    ProviderEndpoints e;
    e.llmBaseUrl = resolve("TTV_LLM_BASE_URL", QStringLiteral("llm_base_url"),
                           QLatin1String(Defaults::kDefaultLlmBaseUrl));
    e.llmApiKey = resolve("TTV_LLM_API_KEY", QStringLiteral("llm_api_key"), {});
    e.llmModel = resolve("TTV_LLM_MODEL", QStringLiteral("llm_model"), {});
    e.ttsBaseUrl = resolve("TTV_TTS_BASE_URL", QStringLiteral("tts_base_url"),
                           QLatin1String(Defaults::kDefaultTtsBaseUrl));
    e.videoGatewayBaseUrl =
        resolve("TTV_VIDEO_GATEWAY_BASE_URL", QStringLiteral("video_gateway_base_url"),
                QLatin1String(Defaults::kDefaultVideoGatewayBaseUrl));
    e.videoGatewayApiKey = resolve("TTV_VIDEO_GATEWAY_API_KEY",
                                   QStringLiteral("video_gateway_api_key"), {});
    e.videoModel = resolve("TTV_VIDEO_MODEL", QStringLiteral("video_model"), {});
    return e;
}

} // namespace TtvStudio::Core
