#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ProviderError.h"
#include "Retry.h"
#include "Transport.h"

namespace TtvStudio::Providers {

inline constexpr char kVideoGatewayProviderName[] = "local_gateway";

// Normalized task states across the submit/poll/download webhook API.
enum class VideoTaskState
{
    Submitted,
    Running,
    Succeeded,
    FailedPermanent, // retry is pointless (bad prompt, auth, content policy…)
    FailedRetryable, // gateway-side timeout/quota — a resubmission may succeed
    Unknown          // unrecognized raw status — treat as still in flight
};

struct VideoSubmitRequest
{
    QString prompt;
    QString model;
    QString aspectRatio;   // e.g. "16:9"
    QString mode;          // gateway operation mode
    QString resolution;    // e.g. "1920x1080" or "1080p" per gateway contract
    int durationSeconds = 8;
};

struct VideoSubmitResult
{
    bool ok = false;
    QString taskId;
    QString pollUrl;       // optional explicit poll endpoint from the gateway
    QString rawStatus;
    ProviderError error;

    static VideoSubmitResult failure(ProviderError err)
    {
        VideoSubmitResult r;
        r.error = std::move(err);
        return r;
    }
};

struct VideoPollResult
{
    bool ok = false;       // false → transport/HTTP-level failure (error set)
    VideoTaskState state = VideoTaskState::Unknown;
    QStringList mediaUrls; // filled when state == Succeeded
    int errorCode = -1;    // gateway error_code, -1 when absent
    QString errorMessage;
    QJsonObject raw;
    ProviderError error;

    static VideoPollResult failure(ProviderError err)
    {
        VideoPollResult r;
        r.error = std::move(err);
        return r;
    }
};

struct VideoDownloadResult
{
    bool ok = false;
    qint64 bytesWritten = 0;
    ProviderError error;

    static VideoDownloadResult failure(ProviderError err)
    {
        VideoDownloadResult r;
        r.error = std::move(err);
        return r;
    }
};

// Adapter for the local desktop-app Webhook video generation gateway
// (submit POST /api/video/generate, status GET /api/status/<id>).
//
// Timeout semantics are deliberately asymmetric:
//   - a *submit* timeout is AmbiguousTimeout — the gateway may have accepted
//     the request, so the caller persists the returned id (if any) and must
//     reconcile instead of blindly resubmitting ("never pay twice"),
//   - a *poll* timeout is also surfaced as ambiguous to the caller.
// Blocking; run on a worker thread.
class VideoGatewayClient
{
public:
    VideoGatewayClient(ITransport &transport, QString baseUrl, QString apiKey, QString model,
                       int requestTimeoutMs = 30'000, int maxAttempts = 3, SleepFn sleep = {});

    VideoSubmitResult submit(const VideoSubmitRequest &request);
    VideoPollResult poll(const QString &taskId, const QString &pollUrl = {});
    VideoDownloadResult download(const QUrl &url, const QString &destinationPath,
                                 qint64 maxBytes);

private:
    ITransport &m_transport;
    QString m_baseUrl;
    QString m_apiKey; // redacted from messages wherever it appears
    QString m_model;
    int m_requestTimeoutMs;
    int m_maxAttempts;
    SleepFn m_sleep;

    QString redact(const QString &message) const;
    HttpRequest baseRequest(const QUrl &url) const;
};

} // namespace TtvStudio::Providers
