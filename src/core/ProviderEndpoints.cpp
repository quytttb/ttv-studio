#include "ProviderEndpoints.h"

#include <QtGlobal>

#include "utils/AppConstants.h"

namespace TtvStudio::Core {

namespace {

QString envOr(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

} // namespace

ProviderEndpoints ProviderEndpoints::fromEnvironment()
{
    ProviderEndpoints e;
    e.llmBaseUrl = envOr("TTV_LLM_BASE_URL", QLatin1String(Defaults::kDefaultLlmBaseUrl));
    e.llmApiKey = qEnvironmentVariable("TTV_LLM_API_KEY");
    e.llmModel = qEnvironmentVariable("TTV_LLM_MODEL");
    e.ttsBaseUrl = envOr("TTV_TTS_BASE_URL", QLatin1String(Defaults::kDefaultTtsBaseUrl));
    e.videoGatewayBaseUrl =
        envOr("TTV_VIDEO_GATEWAY_BASE_URL", QLatin1String(Defaults::kDefaultVideoGatewayBaseUrl));
    e.videoGatewayApiKey = qEnvironmentVariable("TTV_VIDEO_GATEWAY_API_KEY");
    e.videoModel = qEnvironmentVariable("TTV_VIDEO_MODEL");
    return e;
}

} // namespace TtvStudio::Core
