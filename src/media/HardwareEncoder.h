#pragma once

#include <QVector>

#include <QString>
#include <QStringList>

namespace TtvStudio::Media {

// One selectable video encoder backend ("cpu" plus hardware candidates).
struct EncoderInfo
{
    QString id;     // ffmpeg -c:v value, or "cpu" alias resolving to libx264
    QString label;  // human-readable name for the settings UI
    bool hardware = false;
};

// Result of one throwaway probe encode.
struct EncoderProbeResult
{
    bool ok = false;
    qint64 elapsedMs = 0;
    QString error; // non-empty when !ok — last stderr line, secrets-free
};

// Runtime discovery of usable ffmpeg encoders for the render pipeline.
//
// Industry-standard approach (mirrors pyVideoTrans / HwCodecDetect): parse the
// encoders this ffmpeg build advertises, then validate each hardware candidate
// with a tiny lavfi→null encode — advertised is necessary but not sufficient
// (missing drivers/GPU make the encoder fail to initialize). No external GPU
// library is required; nvidia-smi is only consulted for display names.
//
// Blocking subprocess calls — run where QProcess affinity is valid.
class HardwareEncoder
{
public:
    // Ordered backend table shown in the UI (cpu first, then vendor order
    // NVENC → QSV → AMF → VideoToolbox). Pure.
    static QVector<EncoderInfo> candidates();

    // Parse `ffmpeg -encoders` output into raw encoder ids. Pure.
    static QStringList parseEncodersOutput(const QString &output);

    // Encoders advertised by this ffmpeg build (runs `-encoders`).
    static QStringList advertised(const QString &ffmpegBin);

    // Throwaway ~0.3s encode through `encoderId`; ok == exit code 0 means the
    // encoder initializes on this machine.
    static EncoderProbeResult probe(const QString &ffmpegBin, const QString &encoderId);

    // NVIDIA discrete GPUs via `nvidia-smi -L` (display names); empty when the
    // tool is absent or reports no GPU.
    static QStringList nvidiaGpus();

    // Backends that actually work here: "cpu" plus every probed-OK hardware
    // candidate. Runs the full scan (list + probes).
    struct ScanOutcome
    {
        QVector<EncoderInfo> usable;
        QStringList gpuNames;   // NVIDIA display names (informational)
        QString error;          // non-empty when even ffmpeg could not run
    };
    static ScanOutcome scan(const QString &ffmpegBin);

    // ffmpeg args implementing a backend: "-c:v <id>" followed by quality
    // flags. Pure. Unknown ids fall back to the CPU profile.
    static QStringList encodingArgs(const QString &backendId);

    // True when the id names a hardware (non-CPU) backend. Pure.
    static bool isHardware(const QString &backendId);
};

} // namespace TtvStudio::Media
