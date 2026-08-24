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

} // namespace TtvStudio::Paths
