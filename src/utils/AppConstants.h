#pragma once

// Centralized default values, intervals, batch sizes, and other operational
// tunables. Every magic number that was previously sprinkled across data
// models, network configs, controllers, and form views lives here.
//
// Header-only; lives in the `utils` base library so it is includable from
// data / core / network / app layers.
//
// Version constants (DB schema, REST API, Modbus map) live in `Version.h`.

#include <QtGlobal>

namespace TtvStudio::Defaults {

// --- File logging -------------------------------------------------------------
inline constexpr qint64 kLogMaxBytes    = 5LL * 1024 * 1024; // 5 MB
inline constexpr int    kLogKeepBackups = 3;                 // app.log.1 … app.log.3

// --- Provider REST clients (P2) -----------------------------------------------
// Retry budget shared by all providers (jittered exponential backoff).
inline constexpr int kProviderMaxAttempts   = 3;

// LLM (OpenAI-compatible /chat/completions)
inline constexpr int kLlmTimeoutMs          = 120'000;

// Local voice TTS (multipart POST /generate, long-form narration)
inline constexpr int    kTtsTimeoutMs       = 300'000;
inline constexpr qint64 kTtsMinAudioBytes   = 512;     // implausible-body guard
inline constexpr double kMinAudioDurationS  = 0.05;    // ffprobe fail-closed floor

// Video generation gateway (submit / poll / download webhook API)
inline constexpr int    kVideoTaskTimeoutMs      = 900'000; // whole-task budget
inline constexpr int    kVideoRequestTimeoutMs   = 30'000;  // per HTTP request
inline constexpr int    kVideoDownloadTimeoutMs   = 600'000; // clip streaming budget
inline constexpr int    kVideoPollMinMs          = 3'000;   // jittered poll spacing
inline constexpr int    kVideoPollMaxMs          = 5'000;
inline constexpr qint64 kVideoMaxDownloadBytes   = 2'000'000'000LL; // 2 GB cap

// --- Render pipeline: planning & timing (P3) ---------------------------------
inline constexpr double kMinSceneSeconds    = 0.5;   // scenes shorter merge into the previous one
inline constexpr double kClipCountPenalty   = 0.05;  // DP objective penalty per generated clip
inline constexpr double kOptimizerEpsilon   = 1e-6;

// Retime policy: stretching a clip beyond this factor needs freeze-fill or
// regeneration; trimming is always safe.
inline constexpr double kMaxRetimeFactor    = 1.10;
inline constexpr double kMaxFreezeSeconds   = 0.5;

// Normalized-clip encoding target (libx264, CFR).
inline constexpr int    kTargetFps          = 24;
inline constexpr const char *kTargetCodec   = "libx264";
inline constexpr const char *kTargetPreset  = "medium";
inline constexpr int    kTargetCrf          = 18;
inline constexpr const char *kTargetPixFmt  = "yuv420p";

// --- Render device selection (hardware video encoders) -----------------------
// Backend ids are ffmpeg encoder names; "cpu" is always available. Hardware
// candidates are validated at runtime by a throwaway probe encode — being
// advertised by `ffmpeg -encoders` is necessary but not sufficient.
inline constexpr const char *kDefaultRenderBackend = "cpu";
inline constexpr int kEncoderListTimeoutMs  = 8'000;   // `ffmpeg -encoders`
inline constexpr int kEncoderProbeTimeoutMs = 15'000;  // one throwaway encode
inline constexpr int kGpuScanTimeoutMs      = 4'000;   // `nvidia-smi -L`
// Probe clip geometry/duration for the hardware-encoder test.
inline constexpr int    kEncoderProbeWidth    = 320;
inline constexpr int    kEncoderProbeHeight   = 240;
inline constexpr int    kEncoderProbeFps      = 30;
inline constexpr double kEncoderProbeSeconds  = 0.3;
inline constexpr int    kEncoderProbeFrames   = 8;

inline constexpr int    kAudioBitrateKbps   = 192;

// ffmpeg budget for normalize / concat / mux operations.
inline constexpr int    kPostProcessTimeoutMs = 600'000;

// --- Provider endpoint defaults (overridable via env, see ProviderEndpoints) -
inline constexpr const char *kDefaultLlmBaseUrl          = "https://api.vilao.ai/v1";
inline constexpr const char *kDefaultTtsBaseUrl          = "http://127.0.0.1:3900";
inline constexpr const char *kDefaultVideoGatewayBaseUrl = "http://127.0.0.1:8765";

// --- Redub translation (duration-aware dubbing) -------------------------------
// Spoken-language pacing used to convert a segment's original window into a
// target character count for the translator.
inline constexpr double kDubCharsPerSecond        = 14.0;
// Accepted narration length band around the target (translator guidance).
inline constexpr double kDubLengthFloorRatio      = 0.6;
inline constexpr double kDubLengthCeilRatio       = 1.4;
// Transcript segments per LLM translation batch.
inline constexpr int    kTranslationBatchSize     = 10;

// --- Redub ingest (yt-dlp) -----------------------------------------------------
inline constexpr int    kIngestProbeTimeoutMs      = 120'000;   // metadata dump
inline constexpr int    kIngestDownloadTimeoutMs   = 1'800'000; // whole download
inline constexpr qint64 kIngestMaxDownloadBytes    = 2'000'000'000LL;

// --- Redub assembly (original-clock dub) ---------------------------------------
// Narration playback-speed band when fitting a dub line into its original
// window (atempo factors).
inline constexpr double kDubMinRate                = 0.85;
inline constexpr double kDubMaxRate                = 1.25;
// Silence padding floor between consecutive dub lines (seconds).
inline constexpr double kDubWindowEpsilonS         = 0.01;

// Supported discrete generation durations (gateway contract default "4,6,8").
inline constexpr double kDefaultClipDurations[] = {4.0, 6.0, 8.0};

} // namespace TtvStudio::Defaults
