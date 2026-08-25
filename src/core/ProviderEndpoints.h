#pragma once

#include <QString>

namespace TtvStudio::Core {

// Endpoint configuration for the provider REST clients. Values come from the
// environment with AppConstants fallbacks — until P5 ships the Settings UI,
// users point the app at their services via env vars:
//
//   TTV_LLM_BASE_URL            (default https://api.vilao.ai/v1)
//   TTV_LLM_API_KEY
//   TTV_LLM_MODEL               (required for planning to run)
//   TTV_TTS_BASE_URL            (default http://127.0.0.1:3900)
//   TTV_VIDEO_GATEWAY_BASE_URL  (default http://127.0.0.1:8765)
//   TTV_VIDEO_GATEWAY_API_KEY
//   TTV_VIDEO_MODEL             (required for video generation to run)
struct ProviderEndpoints
{
    QString llmBaseUrl;
    QString llmApiKey;
    QString llmModel;
    QString ttsBaseUrl;
    QString videoGatewayBaseUrl;
    QString videoGatewayApiKey;
    QString videoModel;

    static ProviderEndpoints fromEnvironment();

    bool llmConfigured() const { return !llmModel.isEmpty(); }
    bool videoConfigured() const { return !videoModel.isEmpty(); }
};

} // namespace TtvStudio::Core
