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

QString ffmpegBinary()
{
    const QString binDir = qEnvironmentVariable("TTV_STUDIO_FFMPEG_BIN_DIR");
    if (!binDir.isEmpty()) {
#ifdef Q_OS_WIN
        const QString candidate = QDir(binDir).filePath(QStringLiteral("ffmpeg.exe"));
#else
        const QString candidate = QDir(binDir).filePath(QStringLiteral("ffmpeg"));
#endif
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

} // namespace TtvStudio::Paths
