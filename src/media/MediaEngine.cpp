#include "MediaEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "Ffprobe.h"
#include "Subprocess.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Media {

namespace {

QString escapeConcatPath(QString path)
{
    path.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return path;
}

} // namespace

QString FitDecision::actionName() const
{
    switch (action) {
    case FitAction::Trim: return QStringLiteral("trim");
    case FitAction::Retime: return QStringLiteral("retime");
    case FitAction::RetimeFreeze: return QStringLiteral("retime_freeze");
    }
    return QStringLiteral("trim");
}

std::optional<FitDecision> planFit(double sourceDurationSeconds,
                                   double targetDurationSeconds,
                                   int fps,
                                   double maxRetimeFactor,
                                   double maxFreezeSeconds,
                                   FitPlanError *error)
{
    if (error)
        error->message.clear();

    if (targetDurationSeconds <= 0.0) {
        if (error)
            *error = {QStringLiteral("Target scene duration must be positive")};
        return std::nullopt;
    }
    if (sourceDurationSeconds <= 0.0) {
        if (error)
            *error = {QStringLiteral("Source clip duration must be positive")};
        return std::nullopt;
    }

    const double epsilon = 0.5 / qMax(fps, 1); // half a frame

    FitDecision decision;
    decision.sourceDurationSeconds = sourceDurationSeconds;
    decision.targetDurationSeconds = targetDurationSeconds;

    if (sourceDurationSeconds >= targetDurationSeconds - epsilon) {
        decision.action = FitAction::Trim;
        return decision;
    }

    const double factor = targetDurationSeconds / sourceDurationSeconds;
    if (factor <= maxRetimeFactor + 1e-6) {
        decision.action = FitAction::Retime;
        decision.retimeFactor = factor;
        return decision;
    }

    const double residualGap =
        targetDurationSeconds - sourceDurationSeconds * maxRetimeFactor;
    if (residualGap <= maxFreezeSeconds) {
        decision.action = FitAction::RetimeFreeze;
        decision.retimeFactor = maxRetimeFactor;
        decision.freezeFillSeconds = residualGap;
        return decision;
    }

    if (error) {
        *error = {
            QStringLiteral("Clip is %1s but the scene needs %2s (retime factor %3 exceeds "
                           "policy %4 and the gap exceeds the freeze fill budget %5s); "
                           "regenerate with a longer duration")
                .arg(sourceDurationSeconds, 0, 'f', 2)
                .arg(targetDurationSeconds, 0, 'f', 2)
                .arg(factor, 0, 'f', 2)
                .arg(maxRetimeFactor, 0, 'f', 2)
                .arg(maxFreezeSeconds, 0, 'f', 2)};
    }
    return std::nullopt;
}

MediaEngine::MediaEngine(QString ffmpegBin, const Ffprobe *ffprobe)
    : m_ffmpegBin(std::move(ffmpegBin)),
      m_ffprobe(ffprobe)
{
}

bool MediaEngine::fitClip(const QString &sourcePath,
                          const QString &destinationPath,
                          const NormalizeTarget &target,
                          const FitDecision &decision,
                          const VideoEncodeConfig &encode,
                          QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const std::optional<MediaInfo> probe = m_ffprobe->probe(sourcePath);
    if (!probe)
        return setErr(QStringLiteral("ffprobe failed on %1").arg(QFileInfo(sourcePath).fileName()));
    if (!probe->hasVideo)
        return setErr(QStringLiteral("Input has no video stream: %1")
                          .arg(QFileInfo(sourcePath).fileName()));

    // Mirror the media contract: ffmpeg never creates parent directories.
    const QString destDir = QFileInfo(destinationPath).absolutePath();
    if (!QDir().mkpath(destDir))
        return setErr(QStringLiteral("cannot create destination directory %1").arg(destDir));

    QStringList filters;
    if (decision.action == FitAction::Retime || decision.action == FitAction::RetimeFreeze) {
        if (qAbs(decision.retimeFactor - 1.0) > 1e-6)
            filters.append(QStringLiteral("setpts=%1*PTS").arg(decision.retimeFactor, 0, 'f', 6));
        if (decision.freezeFillSeconds > 1e-3)
            filters.append(QStringLiteral("tpad=stop_mode=clone:stop_duration=%1")
                               .arg(decision.freezeFillSeconds, 0, 'f', 3));
    }
    filters.append(QStringLiteral("scale=%1:%2").arg(target.width).arg(target.height));
    filters.append(QStringLiteral("fps=%1").arg(target.fps));

    QStringList fitArgs{QStringLiteral("-y"),
                        QStringLiteral("-i"), sourcePath,
                        QStringLiteral("-vf"), filters.join(QChar(',')),
                        QStringLiteral("-t"),
                        QString::number(decision.targetDurationSeconds, 'f', 3),
                        QStringLiteral("-an")};
    fitArgs << QStringList{QStringLiteral("-c:v"), encode.codec};
    fitArgs << encode.codecArgs;
    fitArgs << QStringList{QStringLiteral("-pix_fmt"), QLatin1String(Defaults::kTargetPixFmt),
                           QStringLiteral("-vsync"), QStringLiteral("cfr"),
                           destinationPath};

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(m_ffmpegBin, fitArgs,
                                                   Defaults::kPostProcessTimeoutMs);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out normalizing the clip"));
    if (!result.ok())
        return setErr(QStringLiteral("ffmpeg failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.section(QChar('\n'), -1)));
    return true;
}

bool MediaEngine::concatClips(const QStringList &clipPaths,
                              const QString &destinationPath,
                              const VideoEncodeConfig &encode,
                              QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (clipPaths.isEmpty())
        return setErr(QStringLiteral("No clips to concatenate"));

    const QString destDir = QFileInfo(destinationPath).absolutePath();
    if (!QDir().mkpath(destDir))
        return setErr(QStringLiteral("cannot create destination directory %1").arg(destDir));

    const QString listPath = destinationPath + QStringLiteral(".concat.txt");
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return setErr(QStringLiteral("cannot write concat list"));
    for (const QString &clip : clipPaths) {
        QFileInfo info(clip);
        listFile.write(QStringLiteral("file '%1'\n")
                           .arg(escapeConcatPath(info.absoluteFilePath()))
                           .toUtf8());
    }
    listFile.close();

    QStringList concatArgs{QStringLiteral("-y"),
                           QStringLiteral("-f"), QStringLiteral("concat"),
                           QStringLiteral("-safe"), QStringLiteral("0"),
                           QStringLiteral("-i"), listPath};
    concatArgs << QStringList{QStringLiteral("-c:v"), encode.codec};
    concatArgs << encode.codecArgs;
    concatArgs << QStringList{QStringLiteral("-pix_fmt"), QLatin1String(Defaults::kTargetPixFmt),
                              QStringLiteral("-vsync"), QStringLiteral("cfr"),
                              QStringLiteral("-movflags"), QStringLiteral("+faststart"),
                              destinationPath};

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(m_ffmpegBin, concatArgs,
                                                   Defaults::kPostProcessTimeoutMs);
    QFile::remove(listPath);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out during concat"));
    if (!result.ok())
        return setErr(QStringLiteral("ffmpeg concat failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.section(QChar('\n'), -1)));
    return true;
}

bool MediaEngine::extractAudio(const QString &sourcePath,
                               const QString &destinationPath,
                               QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    const QString partPath = destinationPath + QStringLiteral(".part");

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_ffmpegBin,
        {QStringLiteral("-y"),
         QStringLiteral("-i"), sourcePath,
         QStringLiteral("-vn"),
         QStringLiteral("-ac"), QStringLiteral("1"),
         QStringLiteral("-ar"), QStringLiteral("16000"),
         QStringLiteral("-acodec"), QStringLiteral("pcm_s16le"),
         QStringLiteral("-f"), QStringLiteral("wav"),
         partPath},
        Defaults::kPostProcessTimeoutMs);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out extracting audio"));
    if (!result.ok())
        return setErr(QStringLiteral("ffmpeg extract failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.section(QChar('\n'), -1)));
    if (!QFile::rename(partPath, destinationPath)) {
        QFile::remove(partPath);
        return setErr(QStringLiteral("cannot publish extracted audio"));
    }
    return true;
}

bool MediaEngine::fitNarration(const QString &sourcePath,
                               const QString &destinationPath,
                               double atempoRate,
                               double windowSeconds,
                               QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    const QString partPath = destinationPath + QStringLiteral(".part");

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_ffmpegBin,
        {QStringLiteral("-y"),
         QStringLiteral("-i"), sourcePath,
         QStringLiteral("-af"),
         QStringLiteral("atempo=%1,apad").arg(atempoRate, 0, 'f', 4),
         QStringLiteral("-t"), QString::number(windowSeconds, 'f', 3),
         QStringLiteral("-ar"), QStringLiteral("16000"),
         QStringLiteral("-ac"), QStringLiteral("1"),
         QStringLiteral("-acodec"), QStringLiteral("pcm_s16le"),
         QStringLiteral("-f"), QStringLiteral("wav"),
         partPath},
        Defaults::kPostProcessTimeoutMs);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out fitting narration"));
    if (!result.ok())
        return setErr(QStringLiteral("ffmpeg fit failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.trimmed().section(QChar('\n'), -1)));
    if (!QFile::rename(partPath, destinationPath)) {
        QFile::remove(partPath);
        return setErr(QStringLiteral("cannot publish fitted narration"));
    }
    return true;
}

bool MediaEngine::concatAudio(const QStringList &clipPaths,
                              const QString &destinationPath,
                              QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (clipPaths.isEmpty())
        return setErr(QStringLiteral("no audio clips to concatenate"));
    QDir().mkpath(QFileInfo(destinationPath).absolutePath());

    const QString listPath = destinationPath + QStringLiteral(".concat.txt");
    QFile listFile(listPath);
    if (!listFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return setErr(QStringLiteral("cannot write concat list"));
    for (const QString &clip : clipPaths) {
        listFile.write(QStringLiteral("file '%1'\n")
                           .arg(QFileInfo(clip).absoluteFilePath().replace(
                               QLatin1Char('\''), QStringLiteral("'\\''")))
                           .toUtf8());
    }
    listFile.close();

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_ffmpegBin,
        {QStringLiteral("-y"),
         QStringLiteral("-f"), QStringLiteral("concat"),
         QStringLiteral("-safe"), QStringLiteral("0"),
         QStringLiteral("-i"), listPath,
         QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"),
         destinationPath},
        Defaults::kPostProcessTimeoutMs);
    QFile::remove(listPath);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out during audio concat"));
    if (!result.ok())
        return setErr(QStringLiteral("audio concat failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.trimmed().section(QChar('\n'), -1)));
    return true;
}

bool MediaEngine::muxNarration(const QString &videoPath,
                               const QString &narrationPath,
                               const QString &destinationPath,
                               int audioBitrateKbps,
                               QString *error) const
{
    const auto setErr = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const QString destDir = QFileInfo(destinationPath).absolutePath();
    if (!QDir().mkpath(destDir))
        return setErr(QStringLiteral("cannot create destination directory %1").arg(destDir));

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        m_ffmpegBin,
        {QStringLiteral("-y"),
         QStringLiteral("-i"), videoPath,
         QStringLiteral("-i"), narrationPath,
         QStringLiteral("-map"), QStringLiteral("0:v:0"),
         QStringLiteral("-map"), QStringLiteral("1:a:0"),
         QStringLiteral("-c:v"), QStringLiteral("copy"),
         QStringLiteral("-c:a"), QStringLiteral("aac"),
         QStringLiteral("-b:a"), QStringLiteral("%1k").arg(audioBitrateKbps),
         QStringLiteral("-shortest"),
         QStringLiteral("-f"), QStringLiteral("mp4"),
         QStringLiteral("-movflags"), QStringLiteral("+faststart"),
         destinationPath},
        Defaults::kPostProcessTimeoutMs);

    if (!result.started)
        return setErr(QStringLiteral("ffmpeg could not be started"));
    if (result.timedOut)
        return setErr(QStringLiteral("ffmpeg timed out during mux"));
    if (!result.ok())
        return setErr(QStringLiteral("ffmpeg mux failed (%1): %2")
                          .arg(result.exitCode)
                          .arg(result.stderrText.section(QChar('\n'), -1)));
    return true;
}

} // namespace TtvStudio::Media
