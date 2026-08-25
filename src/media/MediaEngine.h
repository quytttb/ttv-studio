#pragma once

#include <optional>

#include <QString>
#include <QStringList>

namespace TtvStudio::Media {

class Ffprobe;
struct MediaInfo;

// How one raw clip is fitted to its scene window (mirrors the media contract):
//   trim          — source ≥ target: cut at the target boundary (always safe)
//   retime        — mild slow-down within the retime policy
//   retime_freeze — stretch to policy limit, then clone the last frame
enum class FitAction { Trim, Retime, RetimeFreeze };

struct FitDecision
{
    FitAction action = FitAction::Trim;
    double sourceDurationSeconds = 0.0;
    double targetDurationSeconds = 0.0;
    double retimeFactor = 1.0;      // meaningful for Retime / RetimeFreeze
    double freezeFillSeconds = 0.0; // meaningful for RetimeFreeze

    QString actionName() const;
};

struct FitPlanError
{
    QString message;
};

// Pure decision function: choose trim / retime / freeze-fill for one clip.
// Returns nullopt (+ error set) when the mismatch is too severe and the clip
// must be regenerated with a longer generation duration.
std::optional<FitDecision> planFit(double sourceDurationSeconds,
                                   double targetDurationSeconds,
                                   int fps,
                                   double maxRetimeFactor,
                                   double maxFreezeSeconds,
                                   FitPlanError *error);

struct NormalizeTarget
{
    int width = 1280;
    int height = 720;
    int fps = 24;
};

// FFmpeg/ffprobe subprocess operations for render post-production. Blocking;
// run on a worker thread. All outputs are written via .part → rename where
// the caller passes a final destination path.
class MediaEngine
{
public:
    MediaEngine(QString ffmpegBin, const Ffprobe *ffprobe);

    // Normalize one clip to the target geometry/fps/codec and fit the scene
    // window. Fails closed when the probe fails or no video stream exists.
    // Returns false on failure with `error` populated.
    bool fitClip(const QString &sourcePath,
                 const QString &destinationPath,
                 const NormalizeTarget &target,
                 const FitDecision &decision,
                 QString *error) const;

    // Concatenate already-normalized clips with the concat demuxer, H.264 CFR
    // faststart. `clipPaths` must be non-empty.
    bool concatClips(const QStringList &clipPaths,
                     const QString &destinationPath,
                     QString *error) const;

    // Mux the master narration onto the concatenated video as AAC; video
    // stream is copied untouched.
    bool muxNarration(const QString &videoPath,
                      const QString &narrationPath,
                      const QString &destinationPath,
                      int audioBitrateKbps,
                      QString *error) const;

private:
    QString m_ffmpegBin;
    const Ffprobe *m_ffprobe;
};

} // namespace TtvStudio::Media
