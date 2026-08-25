#pragma once

#include <QVector>

namespace TtvStudio::Render {

// Result of quantizing per-scene target durations onto the discrete
// generation lengths the video gateway supports.
struct OptimizedPlan
{
    QVector<double> generationDurations;
    double totalGenerationSeconds = 0.0;
    double totalErrorSeconds = 0.0;

    int sceneCount() const { return generationDurations.size(); }
};

// Thrown-as-value error: no feasible quantization satisfies the retime policy.
struct OptimizerError
{
    QString message;
};

double retimeFactor(double targetSeconds, double generationSeconds);

// Trimming (generation ≥ target) is always safe; stretching is limited by
// policy.
bool isFeasible(double targetSeconds, double generationSeconds, double maxRetimeFactor);

// Split any target beyond the reachable range into balanced sub-scenes:
// reach = maxDuration * maxRetimeFactor.
QVector<double> splitLongTargets(const QVector<double> &targetDurations,
                                 const QVector<double> &supportedDurations,
                                 double maxRetimeFactor);

// Choose one supported generation duration per scene via dynamic programming.
// Objective: minimize total |generation - target| plus a small clip-count
// penalty. Deterministic for fixed inputs.
// Throws (returns) OptimizerError via `error` when infeasible — check
// `error->message.isEmpty()`.
OptimizedPlan quantizeDurations(const QVector<double> &targetDurations,
                                const QVector<double> &supportedDurations,
                                double maxRetimeFactor,
                                OptimizerError *error);

} // namespace TtvStudio::Render
