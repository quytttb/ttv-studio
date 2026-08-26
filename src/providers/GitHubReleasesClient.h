#pragma once

#include <optional>

#include <QByteArray>
#include <QUrl>
#include <QVector>

#include "ProviderError.h"
#include "Retry.h"
#include "Transport.h"

class QString;

namespace TtvStudio::Providers {

// One downloadable file attached to a GitHub release.
struct ReleaseAsset
{
    QString name;
    QUrl url; // browser_download_url
    qint64 sizeBytes = 0;
};

struct ReleaseInfo
{
    QString tagName;   // raw, e.g. "v0.1.2"
    QString version;   // normalized "0.1.2" (leading 'v' stripped)
    QString notesBody; // markdown release notes
    QUrl htmlUrl;
    QVector<ReleaseAsset> assets;
};

struct ReleaseCheckResult
{
    bool ok = false;
    ReleaseInfo release;
    ProviderError error;

    static ReleaseCheckResult failure(ProviderError err)
    {
        ReleaseCheckResult r;
        r.error = std::move(err);
        return r;
    }
};

struct AssetDownloadResult
{
    bool ok = false;
    qint64 bytesWritten = 0;
    QString filePath;
    ProviderError error;

    static AssetDownloadResult failure(ProviderError err)
    {
        AssetDownloadResult r;
        r.error = std::move(err);
        return r;
    }
};

// GitHub Releases API adapter backing the in-app update check:
//   GET https://api.github.com/repos/<owner>/<repo>/releases/latest
//
// The installer asset is streamed straight to disk via the transport's sink
// path (callers pass a .part path and rename after success). Blocking; run
// where the transport's QNAM affinity holds (main thread for UpdateController).
class GitHubReleasesClient
{
public:
    GitHubReleasesClient(ITransport &transport,
                         QString repoOwner,
                         QString repoName,
                         int checkTimeoutMs = 15'000,
                         int downloadTimeoutMs = 600'000,
                         int maxAttempts = 3,
                         SleepFn sleep = {});

    ReleaseCheckResult latestRelease();

    AssetDownloadResult downloadAsset(const ReleaseAsset &asset,
                                      const QString &destinationPath,
                                      qint64 maxBytes);

    // --- pure helpers (unit-tested without I/O) -----------------------------

    // Parse a /releases/latest JSON body. Fails closed on malformed input.
    static std::optional<ReleaseInfo> parseReleaseJson(const QByteArray &body);

    // Pick the asset matching the host platform: "*_amd64.deb" on Linux,
    // "*.exe" offline installer on Windows. nullopt when nothing matches
    // (e.g. macOS — no published asset type).
    static std::optional<ReleaseAsset> pickAssetForPlatform(const ReleaseInfo &release);

    // Component-wise semver compare of "x.y.z[...]" strings; a leading 'v'
    // is tolerated. Returns >0 when a>b, <0 when a<b, 0 when equal. Missing
    // components count as 0. Anything after '-' is ignored (no pre-release
    // ordering — this project does not publish pre-release tags).
    static int compareVersions(const QString &a, const QString &b);

private:
    HttpRequest baseRequest(const QUrl &url) const;

    ITransport &m_transport;
    QString m_repoOwner;
    QString m_repoName;
    int m_checkTimeoutMs;
    int m_downloadTimeoutMs;
    int m_maxAttempts;
    SleepFn m_sleep;
};

} // namespace TtvStudio::Providers
