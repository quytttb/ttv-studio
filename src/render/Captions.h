#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "SceneTypes.h"

namespace TtvStudio::Render {

struct CaptionCue
{
    int index = 0; // 1-based
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    QString text;
};

// Split narration into caption-sized sentence chunks (≤ 96 chars, splitting
// long sentences on word boundaries).
QStringList splitSentences(const QString &script);

// Distribute captions across the master duration by character weight.
// The last cue always ends exactly at `totalDurationSeconds`.
QVector<CaptionCue> proportionalCues(const QString &script, double totalDurationSeconds);

// Scene-aligned captions: one cue per scene window.
QVector<CaptionCue> sceneCaptions(const QVector<Scene> &scenes);

// Render caption cues as a WebVTT document ("WEBVTT" header, HH:MM:SS.mmm).
QString renderVtt(const QVector<CaptionCue> &cues);

} // namespace TtvStudio::Render
