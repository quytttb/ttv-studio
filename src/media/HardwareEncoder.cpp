#include "HardwareEncoder.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "media/Subprocess.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Media {

namespace {

struct Profile
{
    const char *id;
    const char *label;
    bool hardware;
};

// Vendor preference order: NVENC → QSV → AMF → VideoToolbox (matches the
// common auto-selection heuristics; VAAPI is omitted because it needs a
// hwupload filter chain instead of a plain -c:v swap).
constexpr Profile kProfiles[] = {
    {"cpu",               "CPU · libx264",          false},
    {"h264_nvenc",        "NVIDIA NVENC",           true},
    {"h264_qsv",          "Intel Quick Sync",       true},
    {"h264_amf",          "AMD AMF",                true},
    {"h264_videotoolbox", "Apple VideoToolbox",     true},
};

QString backendId(const QString &id)
{
    return id == QLatin1String("cpu") ? QLatin1String(Defaults::kTargetCodec) : id;
}

} // namespace

QVector<EncoderInfo> HardwareEncoder::candidates()
{
    QVector<EncoderInfo> out;
    for (const Profile &p : kProfiles)
        out.append({QLatin1String(p.id), QLatin1String(p.label), p.hardware});
    return out;
}

QStringList HardwareEncoder::parseEncodersOutput(const QString &output)
{
    // Each advertised encoder line looks like:
    //   V....D h264_nvenc   NVIDIA NVENC H.264 encoder  (codec h264)
    // i.e. one space, six capability flags, whitespace, id, 2+ spaces, name.
    QStringList ids;
    static const QRegularExpression line(
        QStringLiteral("^\\s*[AVSDEF.]{6}\\s+([A-Za-z0-9_]+)\\s{2,}"));
    const QStringList lines = output.split(QChar('\n'));
    for (const QString &raw : lines) {
        const auto match = line.match(raw);
        if (match.hasMatch())
            ids.append(match.captured(1));
    }
    return ids;
}

QStringList HardwareEncoder::advertised(const QString &ffmpegBin)
{
    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        ffmpegBin, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")},
        Defaults::kEncoderListTimeoutMs);
    if (!result.ok())
        return {};
    return parseEncodersOutput(result.stdoutText + QStringLiteral("\n")
                               + result.stderrText);
}

EncoderProbeResult HardwareEncoder::probe(const QString &ffmpegBin,
                                          const QString &encoderId)
{
    EncoderProbeResult out;

    QStringList args{QStringLiteral("-hide_banner"),
                     QStringLiteral("-f"), QStringLiteral("lavfi"),
                     QStringLiteral("-i"),
                     QStringLiteral("color=c=black:s=%1x%2:r=%3:d=%4")
                         .arg(Defaults::kEncoderProbeWidth)
                         .arg(Defaults::kEncoderProbeHeight)
                         .arg(Defaults::kEncoderProbeFps)
                         .arg(Defaults::kEncoderProbeSeconds, 0, 'f', 2)};
    args.append(encodingArgs(encoderId));
    args.append({QStringLiteral("-frames:v"), QString::number(Defaults::kEncoderProbeFrames),
                 QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});

    QElapsedTimer timer;
    timer.start();
    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(ffmpegBin, args,
                                                   Defaults::kEncoderProbeTimeoutMs);
    out.elapsedMs = timer.elapsed();

    if (!result.started) {
        out.error = QStringLiteral("ffmpeg could not be started");
        return out;
    }
    if (result.timedOut) {
        out.error = QStringLiteral("probe timed out");
        return out;
    }
    if (!result.ok()) {
        // Keep it short — the last stderr line carries ffmpeg's reason.
        const QString tail = result.stderrText.trimmed().section(QChar('\n'), -1);
        out.error = tail.isEmpty()
                        ? QStringLiteral("exit code %1").arg(result.exitCode)
                        : tail.left(300);
        return out;
    }
    out.ok = true;
    return out;
}

QStringList HardwareEncoder::nvidiaGpus()
{
    const QString nvidiaSmi =
        QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (nvidiaSmi.isEmpty())
        return {};

    Subprocess subprocess;
    const SubprocessResult result = subprocess.run(
        nvidiaSmi, {QStringLiteral("-L")}, Defaults::kGpuScanTimeoutMs);
    if (!result.ok())
        return {};

    // Lines look like: GPU 0: NVIDIA GeForce RTX 3060 (UUID: GPU-…)
    static const QRegularExpression line(
        QStringLiteral("^GPU \\d+: ([^()]+?) \\(UUID"));
    QStringList names;
    const QStringList lines = result.stdoutText.split(QChar('\n'));
    for (const QString &raw : lines) {
        const auto match = line.match(raw.trimmed());
        if (match.hasMatch())
            names.append(match.captured(1).trimmed());
    }
    return names;
}

HardwareEncoder::ScanOutcome HardwareEncoder::scan(const QString &ffmpegBin)
{
    ScanOutcome out;

    for (const EncoderInfo &candidate : candidates()) {
        if (!candidate.hardware) {
            out.usable.append(candidate);
            continue;
        }
        if (probe(ffmpegBin, candidate.id).ok)
            out.usable.append(candidate);
    }

    out.gpuNames = nvidiaGpus();
    return out;
}

QStringList HardwareEncoder::encodingArgs(const QString &backendIdRaw)
{
    const QString id = backendId(backendIdRaw);

    if (id == QLatin1String("libx264"))
        return {QStringLiteral("-c:v"), id,
                QStringLiteral("-preset"), QLatin1String(Defaults::kTargetPreset),
                QStringLiteral("-crf"), QString::number(Defaults::kTargetCrf)};
    if (id == QLatin1String("h264_nvenc"))
        return {QStringLiteral("-c:v"), id,
                QStringLiteral("-preset"), QStringLiteral("p4"),
                QStringLiteral("-rc"), QStringLiteral("vbr"),
                QStringLiteral("-cq"), QStringLiteral("21"),
                QStringLiteral("-b:v"), QStringLiteral("0")};
    if (id == QLatin1String("h264_qsv"))
        return {QStringLiteral("-c:v"), id,
                QStringLiteral("-preset"), QStringLiteral("fast"),
                QStringLiteral("-global_quality"), QStringLiteral("21")};
    if (id == QLatin1String("h264_amf"))
        return {QStringLiteral("-c:v"), id,
                QStringLiteral("-quality"), QStringLiteral("balanced"),
                QStringLiteral("-rc"), QStringLiteral("cqp"),
                QStringLiteral("-qp_i"), QStringLiteral("22"),
                QStringLiteral("-qp_p"), QStringLiteral("22")};
    if (id == QLatin1String("h264_videotoolbox"))
        return {QStringLiteral("-c:v"), id,
                QStringLiteral("-q:v"), QStringLiteral("45")};

    // Unknown backend — safe CPU fallback.
    return encodingArgs(QStringLiteral("cpu"));
}

bool HardwareEncoder::isHardware(const QString &backendIdRaw)
{
    for (const EncoderInfo &candidate : candidates())
        if (candidate.id == backendIdRaw)
            return candidate.hardware;
    return false;
}

} // namespace TtvStudio::Media
