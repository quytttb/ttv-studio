#include "TtsClient.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>

#include "Transport.h"
#include "media/Ffprobe.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Providers {

namespace {

QByteArray encodeMultipart(const QList<std::pair<QString, QString>> &fields, QByteArray *boundaryOut)
{
    const QByteArray boundary = "ttvStudioFormBoundary7d1a2c4e9b";
    *boundaryOut = boundary;

    QByteArray body;
    for (const auto &[name, value] : fields) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + name.toUtf8() + "\"\r\n\r\n";
        body += value.toUtf8() + "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

} // namespace

TtsClient::TtsClient(ITransport &transport, const Media::Ffprobe &ffprobe, QString baseUrl,
                     int timeoutMs, int maxAttempts, SleepFn sleep)
    : m_transport(transport),
      m_ffprobe(&ffprobe),
      m_baseUrl(std::move(baseUrl)),
      m_timeoutMs(timeoutMs ? timeoutMs : Defaults::kTtsTimeoutMs),
      m_maxAttempts(maxAttempts ? maxAttempts : Defaults::kProviderMaxAttempts),
      m_sleep(std::move(sleep))
{
    while (m_baseUrl.endsWith(u'/'))
        m_baseUrl.chop(1);
}

TtsSynthesisResult TtsClient::synthesize(const TtsRequest &request, const QString &destinationPath)
{
    if (request.text.trimmed().isEmpty())
        return TtsSynthesisResult::failure({ErrorKind::Permanent,
                                            QLatin1String(kTtsProviderName),
                                            QStringLiteral("TTS text must not be empty"),
                                            0});

    QList<std::pair<QString, QString>> fields{
        {QStringLiteral("text"), request.text},
        {QStringLiteral("language"), request.language},
        {QStringLiteral("speed"), QString::number(request.speed, 'f', 2)},
    };
    if (!request.profileId.isEmpty())
        fields.append({QStringLiteral("profile_id"), request.profileId});
    if (!request.instruct.isEmpty())
        fields.append({QStringLiteral("instruct"), request.instruct});
    if (request.maxChunkChars > 0)
        fields.append({QStringLiteral("max_chunk_chars"), QString::number(request.maxChunkChars)});
    if (request.crossfadeMs >= 0)
        fields.append({QStringLiteral("crossfade_ms"), QString::number(request.crossfadeMs)});

    QByteArray boundary;
    HttpRequest httpRequest;
    httpRequest.method = QByteArrayLiteral("POST");
    httpRequest.url = QUrl(m_baseUrl + QStringLiteral("/generate"));
    httpRequest.body = encodeMultipart(fields, &boundary);
    httpRequest.setHeader(QStringLiteral("Content-Type"),
                          QStringLiteral("multipart/form-data; boundary=%1")
                              .arg(QString::fromLatin1(boundary)));

    const QString partPath = destinationPath + QStringLiteral(".part");

    struct FetchResult
    {
        bool ok = false;
        qint64 bytesWritten = 0;
        ProviderError error;
    };

    QElapsedTimer clock;
    clock.start();

    const FetchResult fetch = retryTransient<FetchResult>(
        m_maxAttempts, m_sleep, [&]() -> FetchResult {
            const HttpResponse response =
                m_transport.send(httpRequest, m_timeoutMs, /*maxBodyBytes*/ 512 << 20, partPath);

            if (response.timedOut) {
                return {false, response.bytesReceived,
                        {ErrorKind::Transient, QLatin1String(kTtsProviderName),
                         redactMessage(QStringLiteral("TTS request timed out: %1")
                                           .arg(response.errorText)),
                         response.statusCode}};
            }
            if (!response.networkOk) {
                return {false, response.bytesReceived,
                        {ErrorKind::Transient, QLatin1String(kTtsProviderName),
                         redactMessage(QStringLiteral("TTS transport error: %1")
                                           .arg(response.errorText)),
                         response.statusCode}};
            }
            if (isTransientHttpStatus(response.statusCode)) {
                return {false, response.bytesReceived,
                        {ErrorKind::Transient, QLatin1String(kTtsProviderName),
                         QStringLiteral("TTS provider returned HTTP %1").arg(response.statusCode),
                         response.statusCode}};
            }
            if (response.statusCode >= 400) {
                return {false, response.bytesReceived,
                        {ErrorKind::Permanent, QLatin1String(kTtsProviderName),
                         redactMessage(QStringLiteral("TTS provider returned HTTP %1: %2")
                                           .arg(response.statusCode)
                                           .arg(QString::fromUtf8(response.body.left(300)))),
                         response.statusCode}};
            }
            if (response.bytesReceived < Defaults::kTtsMinAudioBytes) {
                return {false, response.bytesReceived,
                        {ErrorKind::Permanent, QLatin1String(kTtsProviderName),
                         QStringLiteral("TTS provider returned an implausibly small body (%1 bytes)")
                             .arg(response.bytesReceived),
                         response.statusCode}};
            }
            return {true, response.bytesReceived, {}};
        });

    if (!fetch.ok) {
        QFile::remove(partPath);
        return TtsSynthesisResult::failure(fetch.error);
    }

    // Fail closed on audio that does not probe as playable.
    const std::optional<Media::MediaInfo> probe = m_ffprobe->probe(partPath);
    if (!probe || !probe->hasAudio
        || probe->durationSec < Defaults::kMinAudioDurationS) {
        QFile::remove(partPath);
        const QString why = !probe
                                ? QStringLiteral("ffprobe failed on the downloaded audio")
                                : (!probe->hasAudio
                                       ? QStringLiteral("no audio stream found in the downloaded file")
                                       : QStringLiteral("audio duration %.3fs is too short").arg(probe->durationSec));
        return TtsSynthesisResult::failure(
            {ErrorKind::Permanent, QLatin1String(kTtsProviderName), why, 0});
    }

    if (!QFile::rename(partPath, destinationPath)) {
        QFile::remove(partPath);
        return TtsSynthesisResult::failure(
            {ErrorKind::Permanent, QLatin1String(kTtsProviderName),
             QStringLiteral("cannot move %1 into place").arg(QFileInfo(destinationPath).fileName()),
             0});
    }

    TtsSynthesisResult result;
    result.ok = true;
    result.audioPath = destinationPath;
    result.bytesWritten = fetch.bytesWritten;
    result.durationSec = probe->durationSec;
    return result;
}

} // namespace TtvStudio::Providers
