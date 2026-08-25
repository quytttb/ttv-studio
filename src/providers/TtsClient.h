#pragma once

#include <QString>

#include "ProviderError.h"
#include "Retry.h"

namespace TtvStudio::Media {
class Ffprobe;
}

namespace TtvStudio::Providers {

class ITransport;

inline constexpr char kTtsProviderName[] = "local_voice";

struct TtsRequest
{
    QString text;                 // required, non-empty
    QString language = QStringLiteral("vi");
    QString profileId;            // optional voice profile
    QString instruct;             // optional style instruction
    double speed = 1.0;           // 0 < speed ≤ 4
    int maxChunkChars = 0;        // 0 → omitted
    int crossfadeMs = -1;         // negative → omitted
};

struct TtsSynthesisResult
{
    bool ok = false;
    QString audioPath;            // final destination path (post-rename)
    qint64 bytesWritten = 0;
    double durationSec = 0.0;
    ProviderError error;

    static TtsSynthesisResult failure(ProviderError err)
    {
        TtsSynthesisResult r;
        r.error = std::move(err);
        return r;
    }
};

// Adapter for the native local TTS service exposing POST /generate as
// multipart/form-data. The audio body streams into "<destination>.part" and
// is atomically renamed after ffprobe validation (fail-closed on missing or
// implausibly short audio). Blocking; run on a worker thread.
class TtsClient
{
public:
    // `ffprobe` is owned by the caller and must outlive the client.
    TtsClient(ITransport &transport, const Media::Ffprobe &ffprobe, QString baseUrl,
              int timeoutMs = 300'000, int maxAttempts = 3, SleepFn sleep = {});

    TtsSynthesisResult synthesize(const TtsRequest &request, const QString &destinationPath);

private:
    ITransport &m_transport;
    const Media::Ffprobe *m_ffprobe;
    QString m_baseUrl;
    int m_timeoutMs;
    int m_maxAttempts;
    SleepFn m_sleep;
};

} // namespace TtvStudio::Providers
