#pragma once

#include <QString>
#include <QVector>

namespace TtvStudio::Redub {

class Transcript;
class Translation;

// Per-segment dub timing decision (original-clock assembly):
// the narration clip is played back at `atempoRate` so it fits its window.
struct DubTiming
{
    int segmentIndex = 0; // 1-based, matches transcript indices
    double windowSeconds = 0.0;
    double narrationSeconds = 0.0;
    double atempoRate = 1.0;   // clamped to [kDubMinRate, kDubMaxRate]
    bool fitsExactly = false;  // false → the line spills over/clips slightly
};

struct DubPlanError
{
    QString message;
};

// Original-clock assembly planner: pairs translated segment windows with
// their synthesized narration durations and computes playback rates. The
// audio master clock stays the SOURCE timeline — narration is retimed into
// it, never the other way around. Fails closed on empty inputs or index
// mismatches between transcript/translation/narration lists.
bool planDubTiming(const QVector<double> &windowSeconds,     // per transcript segment
                   const QVector<int> &segmentIndexes,
                   const QVector<double> &narrationSeconds,  // same order
                   double minRate,
                   double maxRate,
                   QVector<DubTiming> *out,
                   DubPlanError *error);

} // namespace TtvStudio::Redub
