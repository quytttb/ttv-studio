#pragma once

#include <QString>
#include <QUrl>

class QJsonObject;

namespace TtvStudio::Media {

struct IngestError
{
    QString message;
    bool transient = false; // true → retrying the same URL may succeed
};

// Metadata from `yt-dlp --dump-single-json`.
struct SourceInfo
{
    QString title;
    QString extractor; // e.g. "TikTok", "Douyin"
    double durationSec = 0.0;

    bool valid() const { return durationSec > 0.0; }
};

// Typed wrapper around the yt-dlp CLI for the redub ingest stage
// (REDUB-PIPELINE §3.1). Binary resolution order:
//   1. explicit path containing "/" — must exist, otherwise Permanent,
//   2. TTV_YTDLP_BIN env var,
//   3. PATH lookup ("yt-dlp"),
//   4. [current python interpreter, "-m", "yt_dlp"] fallback.
//
// Cookies: TTV_INGEST_COOKIES_FILE (Netscape format) is appended when set —
// or the core-injected `explicitCookiesFile` when the env var is absent;
// a configured-but-missing file fails closed at command build time.
class YtDlp
{
public:
    explicit YtDlp(QString explicitBin = {}, QString explicitCookiesFile = {});

    // Metadata probe without downloading.
    bool probe(const QUrl &url, SourceInfo *out, IngestError *error) const;

    // Download best ≤1080p MP4 into destinationPath (.part → rename).
    bool download(const QUrl &url, const QString &destinationPath, IngestError *error) const;

private:
    bool buildArguments(QStringList *out, IngestError *error) const;
    QString m_program;     // resolved binary path (or python marker)
    bool m_isPythonModule = false;
    QString m_cookiesFile; // empty → TTV_INGEST_COOKIES_FILE env
};

} // namespace TtvStudio::Media
