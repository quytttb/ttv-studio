#include "VideoGatewayClient.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "Transport.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Providers {

namespace {

// {408, 425, 429, 500, 502, 503, 504} — same set as isTransientHttpStatus.
bool submitStatusTransient(int code)
{
    return isTransientHttpStatus(code);
}

bool submitStatusPermanent(int code)
{
    switch (code) {
    case 400: // bad request
    case 401: // auth
    case 403: // forbidden
    case 404: // unknown endpoint
    case 422: // validation
        return true;
    default:
        return false;
    }
}

VideoTaskState mapRawStatus(const QString &rawLower)
{
    if (rawLower == QLatin1String("pending") || rawLower == QLatin1String("queued"))
        return VideoTaskState::Submitted;
    if (rawLower == QLatin1String("running") || rawLower == QLatin1String("processing"))
        return VideoTaskState::Running;
    if (rawLower == QLatin1String("completed") || rawLower == QLatin1String("succeeded")
        || rawLower == QLatin1String("done"))
        return VideoTaskState::Succeeded;
    if (rawLower == QLatin1String("failed") || rawLower == QLatin1String("cancelled"))
        return VideoTaskState::FailedPermanent; // re-classified by the caller
    return VideoTaskState::Unknown;
}

// A failed payload whose error_code is 0 or whose message mentions timeout /
// quota is retryable — the gateway burned its own attempt without a verdict.
VideoTaskState classifyFailure(const QJsonObject &payload)
{
    const int errorCode = payload.value(QLatin1String("error_code")).toInt(-1);
    const QString message = (payload.value(QLatin1String("error")).toString()
                             + u' '
                             + payload.value(QLatin1String("message")).toString())
                                .toLower();
    if (errorCode == 0 || message.contains(QLatin1String("timeout"))
        || message.contains(QLatin1String("quota")))
        return VideoTaskState::FailedRetryable;
    return VideoTaskState::FailedPermanent;
}

QStringList mediaUrlsFromPayload(const QJsonObject &payload)
{
    QStringList urls;
    const auto results = payload.value(QLatin1String("results")).toArray();
    for (const auto &v : results) {
        if (v.isString() && !v.toString().isEmpty())
            urls.append(v.toString());
    }
    return urls;
}

} // namespace

VideoGatewayClient::VideoGatewayClient(ITransport &transport, QString baseUrl, QString apiKey,
                                       QString model, int requestTimeoutMs, int maxAttempts,
                                       SleepFn sleep)
    : m_transport(transport),
      m_baseUrl(std::move(baseUrl)),
      m_apiKey(std::move(apiKey)),
      m_model(std::move(model)),
      m_requestTimeoutMs(requestTimeoutMs ? requestTimeoutMs : Defaults::kVideoRequestTimeoutMs),
      m_maxAttempts(maxAttempts ? maxAttempts : Defaults::kProviderMaxAttempts),
      m_sleep(std::move(sleep))
{
    while (m_baseUrl.endsWith(u'/'))
        m_baseUrl.chop(1);
}

QString VideoGatewayClient::redact(const QString &message) const
{
    return redactValue(redactMessage(message), m_apiKey);
}

HttpRequest VideoGatewayClient::baseRequest(const QUrl &url) const
{
    HttpRequest request;
    request.url = url;
    request.setHeader(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty())
        request.setHeader(QStringLiteral("X-API-Key"), m_apiKey);
    return request;
}

VideoSubmitResult VideoGatewayClient::submit(const VideoSubmitRequest &request)
{
    if (m_model.trimmed().isEmpty())
        return VideoSubmitResult::failure({ErrorKind::Permanent,
                                           QLatin1String(kVideoGatewayProviderName),
                                           QStringLiteral("VIDEO_MODEL must be configured"),
                                           0});

    QJsonObject body;
    body.insert(QLatin1String("prompt"), request.prompt);
    body.insert(QLatin1String("model"), request.model.isEmpty() ? m_model : request.model);
    body.insert(QLatin1String("aspect_ratio"), request.aspectRatio);
    body.insert(QLatin1String("mode"), request.mode);
    body.insert(QLatin1String("reference_images"), QJsonArray{});
    body.insert(QLatin1String("resolution"), QJsonArray{request.resolution});
    body.insert(QLatin1String("video_length"), request.durationSeconds);

    HttpRequest httpRequest = baseRequest(
        QUrl(m_baseUrl + QStringLiteral("/api/video/generate")));
    httpRequest.method = QByteArrayLiteral("POST");
    httpRequest.body = QJsonDocument(body).toJson(QJsonDocument::Compact);

    struct SubmitRound
    {
        bool ok = false;
        QJsonObject payload;
        ProviderError error;
    };

    const SubmitRound round = retryTransient<SubmitRound>(
        m_maxAttempts, m_sleep, [&]() -> SubmitRound {
            const HttpResponse response =
                m_transport.send(httpRequest, m_requestTimeoutMs, /*maxBodyBytes*/ 4 << 20);

            if (response.timedOut) {
                // Ambiguous: the gateway may have accepted the task. The caller
                // must reconcile — never resubmit blindly.
                return {false, {},
                        {ErrorKind::AmbiguousTimeout, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video submit timed out: %1")
                                    .arg(response.errorText)),
                         response.statusCode}};
            }
            if (!response.networkOk) {
                return {false, {},
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video transport error on submit: %1")
                                    .arg(response.errorText)),
                         response.statusCode}};
            }
            if (submitStatusTransient(response.statusCode)) {
                return {false, {},
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         QStringLiteral("Video gateway returned HTTP %1 on submit")
                             .arg(response.statusCode),
                         response.statusCode}};
            }
            if (submitStatusPermanent(response.statusCode)) {
                return {false, {},
                        {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video gateway returned HTTP %1 on submit: %2")
                                    .arg(response.statusCode)
                                    .arg(QString::fromUtf8(response.body.left(300)))),
                         response.statusCode}};
            }
            if (response.statusCode != 200 && response.statusCode != 201
                && response.statusCode != 202) {
                return {false, {},
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         QStringLiteral("Video gateway returned unexpected HTTP %1 on submit")
                             .arg(response.statusCode),
                         response.statusCode}};
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                return {false, {},
                        {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
                         QStringLiteral("Video gateway returned malformed JSON on submit"),
                         response.statusCode}};
            }
            return {true, doc.object(), {}};
        });

    if (!round.ok)
        return VideoSubmitResult::failure(round.error);

    QString taskId;
    const QJsonValue idValue = round.payload.value(QLatin1String("task_id"));
    if (idValue.isString())
        taskId = idValue.toString();
    else if (idValue.isDouble())
        taskId = QString::number(idValue.toInteger());
    if (taskId.isEmpty()) {
        const QJsonValue altId = round.payload.value(QLatin1String("id"));
        if (altId.isString())
            taskId = altId.toString();
        else if (altId.isDouble())
            taskId = QString::number(altId.toInteger());
    }
    if (taskId.isEmpty()) {
        return VideoSubmitResult::failure(
            {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
             QStringLiteral("Video gateway submit response is missing task_id"),
             0});
    }

    VideoSubmitResult result;
    result.ok = true;
    result.taskId = taskId;
    result.pollUrl = round.payload.value(QLatin1String("poll_url")).toString();
    result.rawStatus = round.payload.value(QLatin1String("status")).toString();
    return result;
}

VideoPollResult VideoGatewayClient::poll(const QString &taskId, const QString &pollUrl)
{
    const QString path =
        pollUrl.startsWith(u"http") ? pollUrl : m_baseUrl + (pollUrl.isEmpty()
              ? QStringLiteral("/api/status/%1").arg(taskId)
              : pollUrl);

    struct PollRound
    {
        bool ok = false;
        QJsonObject payload;
        ProviderError error;
    };

    const PollRound round = retryTransient<PollRound>(
        m_maxAttempts, m_sleep, [&]() -> PollRound {
            HttpRequest httpRequest = baseRequest(QUrl(path));
            httpRequest.method = QByteArrayLiteral("GET");

            const HttpResponse response =
                m_transport.send(httpRequest, m_requestTimeoutMs, /*maxBodyBytes*/ 4 << 20);

            if (response.timedOut) {
                // Unknown remote state — ambiguous, not a failure.
                return {false, {},
                        {ErrorKind::AmbiguousTimeout, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video poll timed out for %1: %2")
                                    .arg(taskId, response.errorText)),
                         response.statusCode}};
            }
            if (!response.networkOk) {
                return {false, {},
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video transport error on poll: %1")
                                    .arg(response.errorText)),
                         response.statusCode}};
            }
            if (submitStatusTransient(response.statusCode)) {
                return {false, {},
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         QStringLiteral("Video gateway returned HTTP %1 on poll")
                             .arg(response.statusCode),
                         response.statusCode}};
            }
            if (submitStatusPermanent(response.statusCode)) {
                return {false, {},
                        {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video gateway returned HTTP %1 on poll: %2")
                                    .arg(response.statusCode)
                                    .arg(QString::fromUtf8(response.body.left(300)))),
                         response.statusCode}};
            }

            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                return {false, {},
                        {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
                         QStringLiteral("Video gateway returned malformed JSON on poll"),
                         response.statusCode}};
            }
            return {true, doc.object(), {}};
        });

    if (!round.ok)
        return VideoPollResult::failure(round.error);

    VideoPollResult result;
    result.raw = round.payload;
    const QString rawStatus = round.payload.value(QLatin1String("status")).toString().toLower();
    result.state = mapRawStatus(rawStatus);
    if (result.state == VideoTaskState::FailedPermanent)
        result.state = classifyFailure(round.payload);
    if (result.state == VideoTaskState::Succeeded)
        result.mediaUrls = mediaUrlsFromPayload(round.payload);

    const QJsonValue errorCode = round.payload.value(QLatin1String("error_code"));
    result.errorCode = errorCode.isDouble() ? errorCode.toInt() : -1;
    result.errorMessage = round.payload.value(QLatin1String("error")).toString();
    if (result.errorMessage.isEmpty())
        result.errorMessage = round.payload.value(QLatin1String("message")).toString();
    result.ok = true;
    return result;
}

VideoDownloadResult VideoGatewayClient::download(const QUrl &url, const QString &destinationPath,
                                                 qint64 maxBytes)
{
    HttpRequest httpRequest = baseRequest(url);
    httpRequest.method = QByteArrayLiteral("GET");

    const QString partPath = destinationPath + QStringLiteral(".part");

    struct DownloadRound
    {
        bool ok = false;
        qint64 bytesWritten = 0;
        ProviderError error;
    };

    const DownloadRound round = retryTransient<DownloadRound>(
        m_maxAttempts, m_sleep, [&]() -> DownloadRound {
            const HttpResponse response =
                m_transport.send(httpRequest, Defaults::kVideoDownloadTimeoutMs, maxBytes,
                                 partPath);
            if (response.timedOut) {
                return {false, response.bytesReceived,
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video download timed out: %1")
                                    .arg(response.errorText)),
                         response.statusCode}};
            }
            if (!response.networkOk) {
                return {false, response.bytesReceived,
                        {ErrorKind::Transient, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video download transport error: %1")
                                    .arg(response.errorText)),
                         response.statusCode}};
            }
            if (!response.statusOk()) {
                return {false, response.bytesReceived,
                        {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
                         redact(QStringLiteral("Video download failed with HTTP %1")
                                    .arg(response.statusCode)),
                         response.statusCode}};
            }
            return {true, response.bytesReceived, {}};
        });

    if (!round.ok)
        return VideoDownloadResult::failure(round.error);

    if (!QFile::rename(partPath, destinationPath)) {
        QFile::remove(partPath);
        return VideoDownloadResult::failure(
            {ErrorKind::Permanent, QLatin1String(kVideoGatewayProviderName),
             QStringLiteral("cannot move downloaded clip into place"), 0});
    }

    VideoDownloadResult result;
    result.ok = true;
    result.bytesWritten = round.bytesWritten;
    return result;
}

} // namespace TtvStudio::Providers
