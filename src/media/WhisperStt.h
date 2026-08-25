#pragma once

#include <QString>

#include "redub/Transcript.h"

namespace TtvStudio::Media {

// Local speech-to-text via the whisper.cpp CLI (subprocess, no SDK).
//
// Binary resolution: TTV_STUDIO_WHISPER_BIN env var → PATH lookup for
// "whisper-cli" then "main". Model file comes from TTV_STUDIO_WHISPER_MODEL
// (ggml .bin) — missing model fails closed before any work starts.
class WhisperStt
{
public:
    struct Config
    {
        QString binaryPath;   // empty → resolve via env/PATH
        QString modelPath;    // empty → TTV_STUDIO_WHISPER_MODEL env
        QString language;     // empty → auto-detect
    };

    explicit WhisperStt(Config config = {});


    // Transcribes a 16 kHz mono WAV into a timestamped transcript.
    // Returns false with `error` set on any failure — callers fail closed.
    bool transcribe(const QString &wavPath, const QString &jobId,
                    TtvStudio::Redub::Transcript *out, QString *error) const;

private:
    QString resolveBinary(QString *error) const;
    Config m_config;
};

} // namespace TtvStudio::Media
