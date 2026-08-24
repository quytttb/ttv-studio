#include "media/Subprocess.h"

#include <QElapsedTimer>
#include <QProcess>

namespace TtvStudio::Media {

Subprocess::Subprocess(QObject *parent)
    : QObject(parent)
{
}

QString Subprocess::truncate(const QByteArray &bytes)
{
    const QByteArray clipped = bytes.left(kMaxCapturedBytes);
    return QString::fromUtf8(clipped);
}

SubprocessResult Subprocess::run(const QString &program,
                                 const QStringList &arguments,
                                 int timeoutMs)
{
    SubprocessResult result;

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);

    process.start(program, arguments);
    if (!process.waitForStarted(10'000)) {
        result.stderrText = QStringLiteral("failed to start: %1").arg(program);
        return result;
    }
    result.started = true;

    // waitFor*() blocks this thread, so QTimer callbacks would never fire.
    // Poll in short slices and enforce the deadline ourselves.
    QElapsedTimer deadline;
    deadline.start();

    bool finished = false;
    while (!finished) {
        finished = process.waitForFinished(50);
        if (!finished && deadline.elapsed() >= qint64(timeoutMs)) {
            process.kill();
            if (!process.waitForFinished(5'000))
                process.terminate();
            result.timedOut = true;
            result.stderrText = truncate(process.readAllStandardError());
            result.stdoutText = truncate(process.readAllStandardOutput());
            return result;
        }
    }

    result.exitCode = process.exitCode();
    result.stdoutText = truncate(process.readAllStandardOutput());
    result.stderrText = truncate(process.readAllStandardError());
    return result;
}

} // namespace TtvStudio::Media
