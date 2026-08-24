#include "media/Ffprobe.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "media/Subprocess.h"

namespace TtvStudio::Media {

namespace {

QString resolveBinary(const QString &explicitPath)
{
    if (!explicitPath.isEmpty())
        return explicitPath;

    const QString binDir = qEnvironmentVariable("TTV_STUDIO_FFMPEG_BIN_DIR");
    if (!binDir.isEmpty()) {
#ifdef Q_OS_WIN
        const QString candidate = QDir(binDir).filePath(QStringLiteral("ffprobe.exe"));
#else
        const QString candidate = QDir(binDir).filePath(QStringLiteral("ffprobe"));
#endif
        if (QFileInfo::exists(candidate))
            return candidate;
    }

    return QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
}

double streamOrFormatDuration(const QJsonObject &format, const QJsonArray &streams)
{
    // Prefer the container duration; fall back to the longest stream duration.
    double best = format.value(QStringLiteral("duration")).toString().toDouble();
    for (const auto &streamValue : streams) {
        const double streamDuration =
            streamValue.toObject().value(QStringLiteral("duration")).toString().toDouble();
        best = qMax(best, streamDuration);
    }
    return best;
}

} // namespace

Ffprobe::Ffprobe(QString binaryPath)
    : m_binaryPath(resolveBinary(std::move(binaryPath)))
{
}

std::optional<MediaInfo> Ffprobe::probe(const QString &mediaPath) const
{
    if (m_binaryPath.isEmpty())
        return std::nullopt;

    const QStringList args{
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"),
        mediaPath,
    };

    const SubprocessResult result = Subprocess().run(m_binaryPath, args);
    if (!result.ok())
        return std::nullopt;

    QJsonParseError parseError{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(result.stdoutText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();

    MediaInfo info;
    info.durationSec = streamOrFormatDuration(format, streams);
    if (info.durationSec <= 0.0)
        return std::nullopt;

    for (const auto &streamValue : streams) {
        const QJsonObject stream = streamValue.toObject();
        const QString codecType =
            stream.value(QStringLiteral("codec_type")).toString();
        if (codecType == QLatin1String("video")) {
            // Cover art in MP3/M4A shows up as a single mjpeg video stream —
            // only a real video track counts.
            const QString codecName =
                stream.value(QStringLiteral("codec_name")).toString();
            if (codecName != QLatin1String("mjpeg")
                && codecName != QLatin1String("png")) {
                info.hasVideo = true;
                info.width = stream.value(QStringLiteral("width")).toInt();
                info.height = stream.value(QStringLiteral("height")).toInt();
            }
        } else if (codecType == QLatin1String("audio")) {
            info.hasAudio = true;
        }
    }
    return info;
}

} // namespace TtvStudio::Media
