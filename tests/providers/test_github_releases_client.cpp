#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "FakeTransport.h"
#include "providers/GitHubReleasesClient.h"

using namespace TtvStudio::Providers;

namespace {

QByteArray releaseJsonBody(const QString &tagName, const QJsonArray &assets)
{
    QJsonObject root;
    root.insert(QStringLiteral("tag_name"), tagName);
    root.insert(QStringLiteral("name"), QStringLiteral("Release %1").arg(tagName));
    root.insert(QStringLiteral("body"), QStringLiteral("## Notes\n- fix a"));
    root.insert(QStringLiteral("html_url"),
                QStringLiteral("https://github.com/quytttb/ttv-studio/releases/tag/%1")
                    .arg(tagName));
    root.insert(QStringLiteral("assets"), assets);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject assetJson(const QString &name, qint64 sizeBytes)
{
    QJsonObject asset;
    asset.insert(QStringLiteral("name"), name);
    asset.insert(QStringLiteral("browser_download_url"),
                 QStringLiteral("https://github.com/quytttb/ttv-studio/releases/download/"
                                "v0.1.2/%1")
                     .arg(name));
    asset.insert(QStringLiteral("size"), double(sizeBytes));
    return asset;
}

} // namespace

class TestGitHubReleasesClient : public QObject
{
    Q_OBJECT

private slots:
    void parsesLatestReleasePayload()
    {
        QJsonArray assets;
        assets.append(assetJson(QStringLiteral("ttv-studio-app_0.1.2_amd64.deb"),
                                71'000'000));
        assets.append(assetJson(QStringLiteral("TtvStudioSetup.exe"), 95'000'000));

        const auto info = GitHubReleasesClient::parseReleaseJson(
            releaseJsonBody(QStringLiteral("v0.1.2"), assets));
        QVERIFY(info.has_value());
        QCOMPARE(info->version, QStringLiteral("0.1.2"));
        QCOMPARE(info->tagName, QStringLiteral("v0.1.2"));
        QVERIFY(info->notesBody.contains(QStringLiteral("fix a")));
        QVERIFY(info->htmlUrl.isValid());
        QCOMPARE(info->assets.size(), 2);
        QCOMPARE(info->assets.first().name,
                 QStringLiteral("ttv-studio-app_0.1.2_amd64.deb"));
        QCOMPARE(info->assets.first().sizeBytes, qint64(71'000'000));
    }

    void parseFailsClosedOnMalformedPayloads()
    {
        QVERIFY(!GitHubReleasesClient::parseReleaseJson(QByteArray())
                     .has_value());
        QVERIFY(!GitHubReleasesClient::parseReleaseJson(
                     QByteArrayLiteral("not json at all"))
                     .has_value());

        // Draft payload without tag_name must be rejected.
        const QJsonObject draft;
        QVERIFY(!GitHubReleasesClient::parseReleaseJson(
                     QJsonDocument(draft).toJson())
                     .has_value());
    }

    void versionCompareSemantics()
    {
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("0.1.1"), QStringLiteral("0.1.2")),
                 -1);
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("0.1.2"), QStringLiteral("0.1.1")),
                 1);
        // Tag prefix tolerated; missing components count as zero.
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("v0.1.2"), QStringLiteral("0.1.2")),
                 0);
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("0.1"), QStringLiteral("0.1.0")),
                 0);
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("0.2.0"), QStringLiteral("0.1.9")),
                 1);
        // Pre-release suffix ignored (project publishes no pre-releases).
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("0.1.3-rc1"), QStringLiteral("0.1.3")),
                 0);
        // Non-numeric junk degrades to zero rather than crashing.
        QCOMPARE(GitHubReleasesClient::compareVersions(
                     QStringLiteral("x.y"), QStringLiteral("0.0")),
                 0);
    }

    void picksPlatformAsset()
    {
        ReleaseInfo release;
        release.tagName = QStringLiteral("v0.1.2");
        release.version = QStringLiteral("0.1.2");

        ReleaseAsset deb;
        deb.name = QStringLiteral("ttv-studio-app_0.1.2_amd64.deb");
        deb.url = QUrl(QStringLiteral("https://example.com/a.deb"));
        deb.sizeBytes = 1000;

        ReleaseAsset exe;
        exe.name = QStringLiteral("TtvStudioSetup.exe");
        exe.url = QUrl(QStringLiteral("https://example.com/setup.exe"));
        exe.sizeBytes = 2000;

        ReleaseAsset zip;
        zip.name = QStringLiteral("sources.zip");
        zip.url = QUrl(QStringLiteral("https://example.com/src.zip"));

        release.assets = {deb, exe, zip};

#ifdef Q_OS_LINUX
        const auto picked = GitHubReleasesClient::pickAssetForPlatform(release);
        QVERIFY(picked.has_value());
        QCOMPARE(picked->name, deb.name);
#elif defined(Q_OS_WIN)
        const auto picked = GitHubReleasesClient::pickAssetForPlatform(release);
        QVERIFY(picked.has_value());
        QCOMPARE(picked->name, exe.name);
#endif

        // Nothing matching on an unsupported platform mix.
        ReleaseInfo empty;
        empty.tagName = QStringLiteral("v1");
        QVERIFY(!GitHubReleasesClient::pickAssetForPlatform(empty).has_value());
    }

    void checkSendsGithubHeadersAndParsesReply()
    {
        FakeTransport transport;
        QJsonArray assets;
        assets.append(assetJson(QStringLiteral("ttv-studio-app_0.1.2_amd64.deb"), 5));
        transport.script.append(
            {true, false, 200, releaseJsonBody(QStringLiteral("v0.1.2"), assets), 0, {}});

        GitHubReleasesClient client(transport, QStringLiteral("quytttb"),
                                    QStringLiteral("ttv-studio"), 5'000, 10'000,
                                    /*maxAttempts*/ 1);
        const auto result = client.latestRelease();

        QVERIFY(result.ok);
        QCOMPARE(result.release.version, QStringLiteral("0.1.2"));
        QCOMPARE(transport.calls.size(), 1);

        const auto &call = transport.calls.first();
        QCOMPARE(call.request.url.toString(),
                 QStringLiteral("https://api.github.com/repos/quytttb/ttv-studio/"
                                "releases/latest"));
        QCOMPARE(call.request.headerValue(QStringLiteral("Accept")),
                 QStringLiteral("application/vnd.github+json"));
        QVERIFY(!call.request
                      .headerValue(QStringLiteral("X-GitHub-Api-Version"))
                      .isEmpty());
        QVERIFY(!call.request.headerValue(QStringLiteral("User-Agent")).isEmpty());
    }

    void rateLimitIsPermanentAndNotRetried()
    {
        FakeTransport transport;
        transport.script.append({true, false, 403, {}, 0, {}});
        int attempts = 0;
        transport.onCall = [&attempts](int, const QString &) { ++attempts; };

        GitHubReleasesClient client(transport, QStringLiteral("quytttb"),
                                    QStringLiteral("ttv-studio"), 5'000, 10'000,
                                    /*maxAttempts*/ 3, [](qint64) {});
        const auto result = client.latestRelease();

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(attempts, 1); // no retry budget burned on a hopeless status
    }

    void transientNetworkErrorRetriesThenSucceeds()
    {
        FakeTransport transport;
        QJsonArray assets;
        assets.append(assetJson(QStringLiteral("ttv-studio-app_0.1.2_amd64.deb"), 5));
        transport.script.append({false, false, 0, {}, 0,
                                 QStringLiteral("connection reset")}); // attempt 1
        transport.script.append(
            {true, false, 200, releaseJsonBody(QStringLiteral("v0.1.2"), assets), 0,
             {}}); // attempt 2

        GitHubReleasesClient client(transport, QStringLiteral("quytttb"),
                                    QStringLiteral("ttv-studio"), 5'000, 10'000,
                                    /*maxAttempts*/ 3, [](qint64) {});
        const auto result = client.latestRelease();

        QVERIFY(result.ok);
        QCOMPARE(transport.calls.size(), 2);
    }

    void downloadStreamsIntoSinkFileAtomically()
    {
        FakeTransport transport;
        transport.sinkPayload = QByteArrayLiteral("DEB-PAYLOAD-BYTES");
        transport.script.append({true, false, 200, {}, 0, {}}); // success reply

        GitHubReleasesClient client(transport, QStringLiteral("quytttb"),
                                    QStringLiteral("ttv-studio"), 5'000, 10'000,
                                    /*maxAttempts*/ 1);

        QTemporaryDir dir;
        const QString destination =
            dir.filePath(QStringLiteral("ttv-studio-app_0.1.2_amd64.deb.part"));
        ReleaseAsset asset;
        asset.name = QStringLiteral("ttv-studio-app_0.1.2_amd64.deb");
        asset.url = QUrl(QStringLiteral("https://example.com/a.deb"));
        asset.sizeBytes = 16;

        const auto result = client.downloadAsset(asset, destination, 1 << 20);

        QVERIFY(result.ok);
        QCOMPARE(result.bytesWritten, qint64(17));
        QCOMPARE(result.filePath, destination);
        QFile file(destination);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArrayLiteral("DEB-PAYLOAD-BYTES"));
    }
};

QTEST_MAIN(TestGitHubReleasesClient)
#include "test_github_releases_client.moc"
