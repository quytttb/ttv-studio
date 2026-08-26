#include "YtDlp.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "Subprocess.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Media {

namespace {

constexpr const char *kFormatFallbackChain =
    "bv*[height<=1080]+ba/b[height<=1080]/bv*+ba/b";

QString envOrEmpty(const char *name)
{
    return qEnvironmentVariable(name);
}

} // namespace

YtDlp::YtDlp(QString explicitBin, QString explicitCookiesFile)
    : m_cookiesFile(std::move(explicitCookiesFile))
{
    // 1. explicit configured path (must contain a separator and exist).
    if (!explicitBin.isEmpty()) {
        if (explicitBin.contains(QLatin1Char('/'))) {
            if (QFileInfo::exists(explicitBin)) {
                m_program = explicitBin;
                return;
            }
            // Permanent misconfiguration: leave unresolved; buildCommand
            // reports it when used.
            return;
        }
        m_program = explicitBin;
        return;
    }

    // 2. TTV_YTDLP_BIN env var.
    const QString fromEnv = envOrEmpty("TTV_YTDLP_BIN");
    if (!fromEnv.isEmpty()) {
        if (fromEnv.contains(QLatin1Char('/')) && !QFileInfo::exists(fromEnv))
            return;
        m_program = fromEnv;
        return;
    }

    // 3. PATH lookup.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (!onPath.isEmpty()) {
        m_program = onPath;
        return;
    }

    // 4. python -m yt_dlp fallback.
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
#ifdef Q_OS_WIN
    const QString pythonWin = QStandardPaths::findExecutable(QStringLiteral("python"));
#endif
    if (!python.isEmpty()) {
        m_program = python;
        m_isPythonModule = true;
    }
}

bool YtDlp::buildArguments(QStringList *out, IngestError *error) const
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = IngestError{message, false};
        return false;
    };

    if (m_program.isEmpty())
        return fail(QStringLiteral(
            "yt-dlp binary not found (configure TTV_YTDLP_BIN or install yt-dlp)"));

    if (m_isPythonModule)
        *out << QStringLiteral("-m") << QStringLiteral("yt_dlp");

    // Contract: env var wins; otherwise the core-injected stored setting.
    QString cookies = envOrEmpty("TTV_INGEST_COOKIES_FILE");
    if (cookies.isEmpty())
        cookies = m_cookiesFile;
    if (!cookies.isEmpty()) {
        // Fail closed: a configured-but-missing cookies file means the
        // operator expected an authenticated download.
        if (!QFileInfo::exists(cookies))
            return fail(QStringLiteral("cookies file does not exist: %1")
                            .arg(cookies));
        *out << QStringLiteral("--cookies") << cookies;
    }
    return true;
}

bool YtDlp::probe(const QUrl &url, SourceInfo *out, IngestError *error) const
{
    auto fail = [error](const QString &message, bool transient) {
        if (error)
            *error = IngestError{message, transient};
        return false;
    };

    QStringList args;
    if (!buildArguments(&args, error))
        return fail(error->message, false);

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_program,
        args << QStringLiteral("--dump-single-json")
             << QStringLiteral("--no-playlist")
             << url.toString(),
        Defaults::kIngestProbeTimeoutMs);

    if (!result.started)
        return fail(QStringLiteral("yt-dlp could not be started"), false);
    if (result.timedOut)
        return fail(QStringLiteral("metadata probe timed out"), true);
    if (!result.ok())
        return fail(QStringLiteral("yt-dlp probe failed (%1): %2")
                        .arg(result.exitCode)
                        .arg(result.stderrText.trimmed().section(QChar('\n'), -1)),
                    false);

    const QJsonDocument doc = QJsonDocument::fromJson(result.stdoutText.toUtf8());
    if (!doc.isObject())
        return fail(QStringLiteral("yt-dlp returned malformed metadata JSON"), false);

    const QJsonObject payload = doc.object();
    SourceInfo info;
    info.title = payload.value(QLatin1String("title")).toString();
    info.extractor = payload.value(QLatin1String("extractor_key")).toString();
    info.durationSec = payload.value(QLatin1String("duration")).toDouble();
    if (!info.valid())
        return fail(QStringLiteral("source has no usable duration"), false);

    if (out)
        *out = info;
    return true;
}

bool YtDlp::download(const QUrl &url, const QString &destinationPath, IngestError *error) const
{
    auto fail = [error](const QString &message, bool transient) {
        if (error)
            *error = IngestError{message, transient};
        return false;
    };

    QStringList args;
    if (!buildArguments(&args, error))
        return fail(error->message, false);

    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    // yt-dlp appends extensions itself; give it a template inside the job dir.
    const QString outputTemplate =
        QFileInfo(destinationPath).absolutePath() + QStringLiteral("/source.%(ext)s");
    const QString partGlob =
        QFileInfo(destinationPath).absolutePath() + QStringLiteral("/source.mp4.part");

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_program,
        args << QStringLiteral("--no-playlist")
            << QStringLiteral("-f") << QLatin1String(kFormatFallbackChain)
            << QStringLiteral("--merge-output-format") << QStringLiteral("mp4")
            << QStringLiteral("--max-filesize")
            << QString::number(Defaults::kIngestMaxDownloadBytes)
            << QStringLiteral("--retries") << QString::number(2)
            << QStringLiteral("-o") << outputTemplate
            << url.toString(),
        Defaults::kIngestDownloadTimeoutMs);

    QFile::remove(partGlob); // never keep partial leftovers

    if (!result.started)
        return fail(QStringLiteral("yt-dlp could not be started"), false);
    if (result.timedOut)
        return fail(QStringLiteral("download timed out"), true);
    if (!result.ok())
        return fail(QStringLiteral("yt-dlp failed (%1): %2")
                        .arg(result.exitCode)
                        .arg(result.stderrText.trimmed().section(QChar('\n'), -1)),
                    false);

    if (!QFile::exists(destinationPath))
        return fail(QStringLiteral("download finished but %1 is missing").arg(
                        QFileInfo(destinationPath).fileName()),
                    false);
    return true;
}

} // namespace TtvStudio::Media
