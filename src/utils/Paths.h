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

// ffmpeg binary resolution (shared with Ffprobe's ffprobe lookup):
//   1. TTV_STUDIO_FFMPEG_BIN_DIR env var (directory holding the binaries),
//   2. `configuredBinDir` (Settings-store value injected by the core layer —
//      env deliberately wins so CI/ops overrides stay authoritative),
//   3. PATH lookup. Empty string when nothing is found — callers fail closed.
QString ffmpegBinary(const QString &configuredBinDir = {});

// Same contract for any sibling tool living next to ffmpeg (ffprobe…).
QString toolBinary(const char *toolName, const QString &configuredBinDir = {});

} // namespace TtvStudio::Paths
