#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace TtvStudio::Media {

struct SubprocessResult
{
    bool started = false;      // process launched successfully
    int exitCode = -1;         // raw exit code (valid when !timedOut && started)
    bool timedOut = false;     // killed after timeoutMs
    QString stderrText;        // captured stderr (truncated to kMaxStderrBytes)
    QString stdoutText;        // captured stdout

    bool ok() const { return started && !timedOut && exitCode == 0; }
};

// Blocking typed wrapper around QProcess for driving external binaries
// (ffmpeg / ffprobe / yt-dlp). Runs on the caller thread using waitFor*();
// pipeline stages are expected to invoke it from worker threads.
class Subprocess : public QObject
{
    Q_OBJECT

public:
    static constexpr int kDefaultTimeoutMs = 30'000;
    static constexpr int kMaxCapturedBytes = 1 << 20; // 1 MiB per stream

    explicit Subprocess(QObject *parent = nullptr);

    SubprocessResult run(const QString &program,
                         const QStringList &arguments,
                         int timeoutMs = kDefaultTimeoutMs);

private:
    static QString truncate(const QByteArray &bytes);
};

} // namespace TtvStudio::Media
