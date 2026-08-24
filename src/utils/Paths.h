#pragma once

#include <QString>

namespace TtvStudio::Paths {

// Storage root resolution order:
//   1. TTV_STUDIO_STORAGE_ROOT environment variable (explicit override),
//   2. <AppDataLocation>/storage  (e.g. ~/.local/share/quytttb/TTV Studio/storage).
// Must be called after QCoreApplication organization/application names are set.
QString storageRoot();

// Per-job artifact directory: <storageRoot>/jobs/<jobId>/.
QString jobsRoot();
QString jobDir(const QString &jobId);

} // namespace TtvStudio::Paths
