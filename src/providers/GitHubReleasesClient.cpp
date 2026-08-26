#include "GitHubReleasesClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace TtvStudio::Providers {

inline constexpr char kGitHubProviderName[] = "github_releases";

namespace {

// "v0.1.2" / "V0.1.2" → ["0","1","2"]; pre-release suffix after '-' ignored.
QStringList versionComponents(const QString &raw)
{
    QString text = raw.trimmed();
    if (!text.isEmpty()
        && (text.startsWith(QLatin1Char('v')) || text.startsWith(QLatin1Char('V'))))
        text.remove(0, 1);
    const int dash = text.indexOf(QLatin1Char('-'));
    if (dash >= 0)
        text.truncate(dash);
    return text.split(QLatin1Char('.'));
}

long long componentValue(const QStringList &parts, int index)
{
    if (index >= parts.size())
        return 0; // missing components count as 0 ("0.1" == "0.1.0")
    bool valid = false;
    const long long value = parts.at(index).toLongLong(&valid);
    return valid ? value : 0;
}

} // namespace

GitHubReleasesClient::GitHubReleasesClient(ITransport &transport,
                                           QString repoOwner,
                                           QString repoName,
                                           int checkTimeoutMs,
                                           int downloadTimeoutMs,
                                           int maxAttempts,
                                           SleepFn sleep)
    : m_transport(transport),
      m_repoOwner(std::move(repoOwner)),
      m_repoName(std::move(repoName)),
      m_checkTimeoutMs(checkTimeoutMs),
      m_downloadTimeoutMs(downloadTimeoutMs),
      m_maxAttempts(maxAttempts),
      m_sleep(std::move(sleep))
{
}

HttpRequest GitHubReleasesClient::baseRequest(const QUrl &url) const
{
    HttpRequest request;
    request.url = url;
    request.setHeader(QStringLiteral("Accept"),
                      QStringLiteral("application/vnd.github+json"));
    request.setHeader(QStringLiteral("X-GitHub-Api-Version"), QStringLiteral("2022-11-28"));
    request.setHeader(QStringLiteral("User-Agent"), QStringLiteral("ttv-studio-updater"));
    return request;
}

ReleaseCheckResult GitHubReleasesClient::latestRelease()
{
    const QUrl url = QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                         .arg(m_repoOwner, m_repoName);

    return retryTransient<ReleaseCheckResult>(
        m_maxAttempts, m_sleep, [this, &url]() -> ReleaseCheckResult {
            const HttpResponse response =
                m_transport.send(baseRequest(url), m_checkTimeoutMs,
                                 /*maxBodyBytes*/ 4 << 20);
            if (response.timedOut) {
                // A check timeout carries no side effects; retrying is safe.
                return ReleaseCheckResult::failure(
                    {ErrorKind::Transient, QLatin1String(kGitHubProviderName),
                     QStringLiteral("Update check timed out"), response.statusCode});
            }
            if (!response.networkOk) {
                return ReleaseCheckResult::failure(
                    {ErrorKind::Transient, QLatin1String(kGitHubProviderName),
                     QStringLiteral("Network error during update check"),
                     response.statusCode});
            }
            if (response.statusCode == 404 || response.statusCode == 403) {
                // 404: no published release yet; 403: rate limited — both final.
                return ReleaseCheckResult::failure(
                    {ErrorKind::Permanent, QLatin1String(kGitHubProviderName),
                     QStringLiteral("GitHub API returned HTTP %1")
                         .arg(response.statusCode),
                     response.statusCode});
            }
            if (!response.statusOk()) {
                return ReleaseCheckResult::failure(
                    {ErrorKind::Transient, QLatin1String(kGitHubProviderName),
                     QStringLiteral("HTTP %1 from GitHub API").arg(response.statusCode),
                     response.statusCode});
            }

            std::optional<ReleaseInfo> parsed = parseReleaseJson(response.body);
            if (!parsed) {
                return ReleaseCheckResult::failure(
                    {ErrorKind::Permanent, QLatin1String(kGitHubProviderName),
                     QStringLiteral("Unrecognized release payload"), response.statusCode});
            }
            ReleaseCheckResult ok;
            ok.ok = true;
            ok.release = std::move(*parsed);
            return ok;
        });
}

AssetDownloadResult GitHubReleasesClient::downloadAsset(const ReleaseAsset &asset,
                                                        const QString &destinationPath,
                                                        qint64 maxBytes)
{
    HttpRequest request = baseRequest(asset.url);
    // Binary asset — do not ask for JSON.
    request.setHeader(QStringLiteral("Accept"), QStringLiteral("application/octet-stream"));

    return retryTransient<AssetDownloadResult>(
        m_maxAttempts, m_sleep,
        [this, request, &asset, &destinationPath, maxBytes]() -> AssetDownloadResult {
            // A failed attempt leaves no sink file behind (the transport
            // removes it), so each retry streams into a clean path.
            const HttpResponse response =
                m_transport.send(request, m_downloadTimeoutMs, maxBytes, destinationPath);
            if (response.timedOut) {
                return AssetDownloadResult::failure(
                    {ErrorKind::Transient, QLatin1String(kGitHubProviderName),
                     QStringLiteral("Download of %1 timed out").arg(asset.name),
                     response.statusCode});
            }
            if (!response.networkOk) {
                return AssetDownloadResult::failure(
                    {ErrorKind::Transient, QLatin1String(kGitHubProviderName),
                     QStringLiteral("Network error downloading %1").arg(asset.name),
                     response.statusCode});
            }
            if (!response.statusOk()) {
                return AssetDownloadResult::failure(
                    {ErrorKind::Permanent, QLatin1String(kGitHubProviderName),
                     QStringLiteral("HTTP %1 while downloading %2")
                         .arg(response.statusCode)
                         .arg(asset.name),
                     response.statusCode});
            }

            AssetDownloadResult ok;
            ok.ok = true;
            ok.bytesWritten = response.bytesReceived;
            ok.filePath = destinationPath;
            return ok;
        });
}

std::optional<ReleaseInfo> GitHubReleasesClient::parseReleaseJson(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return std::nullopt;

    const QJsonObject root = doc.object();
    const QString tagName = root.value(QLatin1String("tag_name")).toString().trimmed();
    if (tagName.isEmpty())
        return std::nullopt; // draft payloads lack tag_name — fail closed

    ReleaseInfo info;
    info.tagName = tagName;
    info.version = tagName.startsWith(QLatin1Char('v')) ? tagName.mid(1) : tagName;
    info.notesBody = root.value(QLatin1String("body")).toString();
    info.htmlUrl = QUrl(root.value(QLatin1String("html_url")).toString());

    const QJsonArray assets = root.value(QLatin1String("assets")).toArray();
    for (const auto &value : assets) {
        const QJsonObject obj = value.toObject();
        ReleaseAsset asset;
        asset.name = obj.value(QLatin1String("name")).toString();
        asset.url = QUrl(obj.value(QLatin1String("browser_download_url")).toString());
        asset.sizeBytes = static_cast<qint64>(obj.value(QLatin1String("size")).toDouble(0));
        if (asset.name.isEmpty() || !asset.url.isValid())
            continue;
        info.assets.append(asset);
    }
    return info;
}

std::optional<ReleaseAsset>
GitHubReleasesClient::pickAssetForPlatform(const ReleaseInfo &release)
{
#if defined(Q_OS_WIN)
    for (const ReleaseAsset &asset : release.assets)
        if (asset.name.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
            return asset;
#elif defined(Q_OS_LINUX)
    for (const ReleaseAsset &asset : release.assets)
        if (asset.name.endsWith(QLatin1String("_amd64.deb"), Qt::CaseInsensitive))
            return asset;
#endif
    return std::nullopt;
}

int GitHubReleasesClient::compareVersions(const QString &a, const QString &b)
{
    const QStringList ac = versionComponents(a);
    const QStringList bc = versionComponents(b);
    const int count = qMax(ac.size(), bc.size());
    for (int i = 0; i < count; ++i) {
        if (const long long av = componentValue(ac, i), bv = componentValue(bc, i);
            av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

} // namespace TtvStudio::Providers
