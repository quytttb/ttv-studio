#include "SettingsStore.h"

#include <QSettings>

#include "utils/AppConstants.h"

namespace TtvStudio::Core {

namespace {

constexpr const char *kGroup = "providers";

} // namespace

QString SettingsStore::resolvedValue(const char *envName, const QString &settingKey,
                                     const QString &fallback)
{
    const QString fromEnv = qEnvironmentVariable(envName);
    if (!fromEnv.isEmpty())
        return fromEnv;
    const QString stored = storedValue(settingKey);
    if (!stored.isEmpty())
        return stored;
    return fallback;
}

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
{
    load();
}

SettingsStore *SettingsStore::create(QQmlEngine *engine, QJSEngine *)
{
    return new SettingsStore(engine);
}

void SettingsStore::load()
{
    QSettings settings;
    settings.beginGroup(kGroup);
    m_llmBaseUrl = settings.value(QStringLiteral("llm_base_url"),
                                  QLatin1String(Defaults::kDefaultLlmBaseUrl))
                       .toString();
    m_llmApiKey = settings.value(QStringLiteral("llm_api_key")).toString();
    m_llmModel = settings.value(QStringLiteral("llm_model")).toString();
    m_ttsBaseUrl = settings.value(QStringLiteral("tts_base_url"),
                                  QLatin1String(Defaults::kDefaultTtsBaseUrl))
                       .toString();
    m_videoGatewayBaseUrl =
        settings.value(QStringLiteral("video_gateway_base_url"),
                       QLatin1String(Defaults::kDefaultVideoGatewayBaseUrl))
            .toString();
    m_videoGatewayApiKey = settings.value(QStringLiteral("video_gateway_api_key")).toString();
    m_videoModel = settings.value(QStringLiteral("video_model")).toString();
    m_whisperBin = settings.value(QStringLiteral("whisper_bin")).toString();
    m_whisperModel = settings.value(QStringLiteral("whisper_model")).toString();
    m_ffmpegBinDir = settings.value(QStringLiteral("ffmpeg_bin_dir")).toString();
    m_ytdlpBin = settings.value(QStringLiteral("ytdlp_bin")).toString();
    m_ingestCookiesFile =
        settings.value(QStringLiteral("ingest_cookies_file")).toString();
    const QString backend =
        settings.value(QStringLiteral("render_backend"),
                       QLatin1String(Defaults::kDefaultRenderBackend))
            .toString();
    m_renderBackend = backend.isEmpty() ? QLatin1String(Defaults::kDefaultRenderBackend)
                                        : backend;
}

QString SettingsStore::storedValue(const QString &key)
{
    QSettings settings;
    settings.beginGroup(kGroup);
    return settings.value(key).toString();
}

void SettingsStore::setValue(const QString &key, const QString &value)
{
    QSettings settings;
    settings.beginGroup(kGroup);
    settings.setValue(key, value);
}

#define SETTER_IMPL(name, prop, key)                                                     \
    void SettingsStore::set##name(const QString &value)                                  \
    {                                                                                    \
        if (m_##prop == value)                                                           \
            return;                                                                      \
        m_##prop = value;                                                                \
        setValue(QLatin1String(key), value);                                             \
        Q_EMIT prop##Changed();                                                          \
    }

SETTER_IMPL(LlmBaseUrl, llmBaseUrl, "llm_base_url")
SETTER_IMPL(LlmApiKey, llmApiKey, "llm_api_key")
SETTER_IMPL(LlmModel, llmModel, "llm_model")
SETTER_IMPL(TtsBaseUrl, ttsBaseUrl, "tts_base_url")
SETTER_IMPL(VideoGatewayBaseUrl, videoGatewayBaseUrl, "video_gateway_base_url")
SETTER_IMPL(VideoGatewayApiKey, videoGatewayApiKey, "video_gateway_api_key")
SETTER_IMPL(VideoModel, videoModel, "video_model")
SETTER_IMPL(WhisperBin, whisperBin, "whisper_bin")
SETTER_IMPL(WhisperModel, whisperModel, "whisper_model")
SETTER_IMPL(FfmpegBinDir, ffmpegBinDir, "ffmpeg_bin_dir")
SETTER_IMPL(YtdlpBin, ytdlpBin, "ytdlp_bin")
SETTER_IMPL(IngestCookiesFile, ingestCookiesFile, "ingest_cookies_file")
SETTER_IMPL(RenderBackend, renderBackend, "render_backend")

} // namespace TtvStudio::Core
