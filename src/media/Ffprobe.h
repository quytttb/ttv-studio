#pragma once

#include <optional>

#include <QString>

class QJsonObject;

namespace TtvStudio::Media {

struct MediaInfo
{
    double durationSec = 0.0;
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
};

// ffprobe front-end. Binary resolution order:
//   1. TTV_STUDIO_FFMPEG_BIN_DIR env var (directory holding ffmpeg/ffprobe),
//   2. PATH lookup.
class Ffprobe
{
public:
    explicit Ffprobe(QString binaryPath = {});

    // Returns std::nullopt when the probe fails or the JSON is malformed —
    // callers must treat sources without a valid probe as unusable.
    std::optional<MediaInfo> probe(const QString &mediaPath) const;

private:
    QString m_binaryPath;
};

} // namespace TtvStudio::Media
