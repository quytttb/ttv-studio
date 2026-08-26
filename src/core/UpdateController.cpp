#include "UpdateController.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>

#include "Version_generated.h"
#include "media/Subprocess.h"
#include "providers/GitHubReleasesClient.h"
#include "providers/QNamTransport.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Core {

UpdateController::UpdateController(QObject *parent)
    : QObject(parent),
      m_currentVersion(QStringLiteral("%1.%2.%3")
                           .arg(Version::kAppMajor)
                           .arg(Version::kAppMinor)
                           .arg(Version::kAppPatch))
{
    m_progressTimer.setInterval(300);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        if (m_pendingAssetSize <= 0 || m_state != QStringLiteral("downloading"))
            return;
        const QString partPath = downloadDir() + QLatin1Char('/') + m_pendingAssetName
                                 + QStringLiteral(".part");
        const qint64 written = QFileInfo::exists(partPath) ? QFileInfo(partPath).size() : 0;
        m_downloadProgress = qBound(0.0, qreal(written) / qreal(m_pendingAssetSize), 1.0);
        Q_EMIT progressChanged();
    });
}

UpdateController *UpdateController::create(QQmlEngine *engine, QJSEngine *)
{
    return new UpdateController(engine);
}

bool UpdateController::busy() const
{
    return m_state == QLatin1String("checking")
           || m_state == QLatin1String("downloading")
           || m_state == QLatin1String("installing");
}

void UpdateController::setState(const QString &state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void UpdateController::setError(const QString &message)
{
    m_errorMessage = message;
    Q_EMIT errorChanged();
    if (!message.isEmpty())
        setState(QStringLiteral("error"));
}

void UpdateController::resetRelease()
{
    if (m_latestVersion.isEmpty() && m_releaseNotes.isEmpty() && !m_releaseUrl.isValid())
        return;
    m_latestVersion.clear();
    m_releaseNotes.clear();
    m_releaseUrl.clear();
    Q_EMIT releaseChanged();
}

QString UpdateController::downloadDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/ttv-studio-update");
    QDir().mkpath(dir);
    return dir;
}

void UpdateController::scheduleStartupCheck()
{
    QTimer::singleShot(Defaults::kUpdateStartupCheckDelayMs, this,
                       &UpdateController::checkForUpdates);
}

void UpdateController::dismissError()
{
    setError(QString());
    if (m_state == QLatin1String("error"))
        setState(QStringLiteral("idle"));
}

void UpdateController::checkForUpdates()
{
    if (busy())
        return;

    setError(QString());
    resetRelease();
    setState(QStringLiteral("checking"));

    QThreadPool::globalInstance()->start([this] {
        std::optional<Providers::QNamTransport> transport;
        transport.emplace(); // created on the worker thread that runs it
        Providers::GitHubReleasesClient client(
            *transport, QLatin1String(Defaults::kUpdateRepoOwner),
            QLatin1String(Defaults::kUpdateRepoName), Defaults::kUpdateCheckTimeoutMs,
            Defaults::kUpdateDownloadTimeoutMs);

        const Providers::ReleaseCheckResult result = client.latestRelease();

        QMetaObject::invokeMethod(
            this,
            [this, result] {
                if (m_state != QLatin1String("checking"))
                    return; // a newer user action superseded this check

                if (!result.ok) {
                    setError(result.error.message);
                    return;
                }

                m_latestVersion = result.release.version;
                m_releaseNotes = result.release.notesBody;
                m_releaseUrl = result.release.htmlUrl;

                const int cmp =
                    Providers::GitHubReleasesClient::compareVersions(
                        result.release.version, m_currentVersion);
                setState(cmp > 0 ? QStringLiteral("available")
                                 : QStringLiteral("uptodate"));
                Q_EMIT releaseChanged();
            },
            Qt::QueuedConnection);
    });
}

void UpdateController::downloadUpdate()
{
    if (m_state != QLatin1String("available") && m_state != QLatin1String("error"))
        return;

    setError(QString());

    // Resolve the platform asset first (cheap, on this thread): the release
    // metadata from the last check is needed for name/size. If the metadata is
    // gone (fresh controller), re-check first — the UI flow guarantees it.
    // Here: fetch fresh inside the worker to avoid stale asset URLs.
    setState(QStringLiteral("downloading"));
    m_downloadProgress = -1.0;
    Q_EMIT progressChanged();

    QThreadPool::globalInstance()->start([this] {
        std::optional<Providers::QNamTransport> transport;
        transport.emplace();
        Providers::GitHubReleasesClient client(
            *transport, QLatin1String(Defaults::kUpdateRepoOwner),
            QLatin1String(Defaults::kUpdateRepoName), Defaults::kUpdateCheckTimeoutMs,
            Defaults::kUpdateDownloadTimeoutMs);

        const Providers::ReleaseCheckResult check = client.latestRelease();
        if (check.ok) {
            const auto asset =
                Providers::GitHubReleasesClient::pickAssetForPlatform(check.release);
            if (asset) {
                const QString partPath =
                    downloadDir() + QLatin1Char('/') + asset->name + QStringLiteral(".part");

                QMetaObject::invokeMethod(
                    this,
                    [this, name = asset->name, size = asset->sizeBytes] {
                        m_pendingAssetName = name;
                        m_pendingAssetSize = size;
                        if (size > 0)
                            m_downloadProgress = 0.0;
                        Q_EMIT progressChanged();
                        m_progressTimer.start();
                    },
                    Qt::QueuedConnection);

                const Providers::AssetDownloadResult download =
                    client.downloadAsset(*asset, partPath,
                                         Defaults::kUpdateMaxDownloadBytes);

                QMetaObject::invokeMethod(
                    this,
                    [this, download, finalPath = downloadDir() + QLatin1Char('/')
                                            + asset->name] {
                        m_progressTimer.stop();

                        if (m_state != QLatin1String("downloading")) {
                            // Superseded (app shutting down / re-check).
                            QFile::remove(finalPath + QStringLiteral(".part"));
                            return;
                        }

                        if (!download.ok) {
                            QFile::remove(finalPath + QStringLiteral(".part"));
                            setError(download.error.message);
                            return;
                        }

                        // Atomic publish: .part → final (repo durability rule).
                        QFile::remove(finalPath);
                        if (!QFile::rename(finalPath + QStringLiteral(".part"),
                                           finalPath)) {
                            QFile::remove(finalPath + QStringLiteral(".part"));
                            setError(QStringLiteral(
                                "cannot move downloaded file into place"));
                            return;
                        }

                        m_downloadProgress = 1.0;
                        Q_EMIT progressChanged();
                        setState(QStringLiteral("downloaded"));
                    },
                    Qt::QueuedConnection);
                return;
            }
        }

        QMetaObject::invokeMethod(
            this,
            [this, message = check.ok ? QStringLiteral(
                          "không tìm thấy bản cài cho nền tảng này")
                                      : check.error.message] {
                if (m_state == QLatin1String("downloading"))
                    setError(message);
            },
            Qt::QueuedConnection);
    });
}

void UpdateController::installUpdate()
{
    if (m_state != QLatin1String("downloaded"))
        return;

    setState(QStringLiteral("installing"));

    const QString filePath = downloadDir() + QLatin1Char('/') + m_pendingAssetName;

#if defined(Q_OS_WIN)
    // QTIFW offline installer replaces the running app; hand over and quit so
    // files are not locked.
    if (QProcess::startDetached(filePath, {})) {
        QMetaObject::invokeMethod(
            qApp,
            [] { QCoreApplication::quit(); },
            Qt::QueuedConnection);
        return;
    }
    setError(QStringLiteral("cannot launch installer"));
#elif defined(Q_OS_LINUX)
    QThreadPool::globalInstance()->start([this, filePath] {
        Media::Subprocess subprocess;

        const QString pkexec =
            QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        const QString apt = QStandardPaths::findExecutable(QStringLiteral("apt"));

        QString message;
        bool ok = false;
        if (!pkexec.isEmpty() && !apt.isEmpty()) {
            // polkit shows a graphical admin-password prompt for this.
            const auto result = subprocess.run(pkexec,
                                               {apt, QStringLiteral("install"),
                                                QStringLiteral("-y"), filePath},
                                               Defaults::kPostProcessTimeoutMs);
            ok = result.ok();
            if (!ok)
                message = result.stderrText.section(QChar('\n'), -1).left(300);
        } else {
            message = QStringLiteral("pkexec/apt không khả dụng");
        }

        QMetaObject::invokeMethod(
            this,
            [this, ok, message, filePath] {
                if (ok) {
                    QFile::remove(filePath); // consumed by dpkg
                    setState(QStringLiteral("installed"));
                    return;
                }
                // Fall back to revealing the file for manual install.
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
                setError(message.isEmpty()
                             ? QStringLiteral("cài đặt tự động thất bại — file đã "
                                              "được mở trong trình quản lý file")
                             : message);
            },
            Qt::QueuedConnection);
    });
#else
    setError(QStringLiteral("nền tảng này chưa hỗ trợ cập nhật tự động"));
#endif
}

} // namespace TtvStudio::Core
