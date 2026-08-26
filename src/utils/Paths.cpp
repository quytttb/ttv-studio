#include "utils/Paths.h"

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

namespace TtvStudio::Paths {

QString storageRoot()
{
    const QString overrideRoot = qEnvironmentVariable("TTV_STUDIO_STORAGE_ROOT");
    if (!overrideRoot.isEmpty())
        return QDir(overrideRoot).absolutePath();

    const QString appData =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appData + QStringLiteral("/storage");
}

QString jobsRoot()
{
    return storageRoot() + QStringLiteral("/jobs");
}

QString jobDir(const QString &jobId)
{
    return jobsRoot() + QLatin1Char('/') + jobId;
}

namespace {

QString dirCandidate(const QString &binDir, const char *toolName)
{
#ifdef Q_OS_WIN
    return QDir(binDir).filePath(QStringLiteral("%1.exe").arg(QLatin1String(toolName)));
#else
    Q_UNUSED(toolName);
    return QDir(binDir).filePath(QLatin1String(toolName));
#endif
}

} // namespace

QString toolBinary(const char *toolName, const QString &configuredBinDir)
{
    // 1. Environment override wins over the stored setting by contract.
    const QString envDir = qEnvironmentVariable("TTV_STUDIO_FFMPEG_BIN_DIR");
    if (!envDir.isEmpty()) {
        const QString candidate = dirCandidate(envDir, toolName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    // 2. Configured directory (Settings-store value injected by core).
    if (!configuredBinDir.isEmpty()) {
        const QString candidate = dirCandidate(configuredBinDir, toolName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    // 3. PATH.
    return QStandardPaths::findExecutable(QLatin1String(toolName));
}

QString ffmpegBinary(const QString &configuredBinDir)
{
    return toolBinary("ffmpeg", configuredBinDir);
}

} // namespace TtvStudio::Paths
