#include "DurationOptimizer.h"

#include <algorithm>
#include <limits>

#include <QtMath>

#include "utils/AppConstants.h"

namespace TtvStudio::Render {

namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

// Grid step shared by all supported durations (1.0 for whole numbers like
// 4/6/8, else 0.5).
double granularity(const QVector<double> &options)
{
    bool wholeNumbers = true;
    for (double option : options) {
        if (qAbs(option - qRound64(option)) > Defaults::kOptimizerEpsilon) {
            wholeNumbers = false;
            break;
        }
    }
    return wholeNumbers ? 1.0 : 0.5;
}

} // namespace

double retimeFactor(double targetSeconds, double generationSeconds)
{
    return targetSeconds / generationSeconds;
}

bool isFeasible(double targetSeconds, double generationSeconds, double maxRetimeFactor)
{
    return generationSeconds >= targetSeconds - Defaults::kOptimizerEpsilon
           || retimeFactor(targetSeconds, generationSeconds) <= maxRetimeFactor + Defaults::kOptimizerEpsilon;
}

QVector<double> splitLongTargets(const QVector<double> &targetDurations,
                                 const QVector<double> &supportedDurations,
                                 double maxRetimeFactor)
{
    double maxDuration = 0.0;
    for (double d : supportedDurations)
        maxDuration = qMax(maxDuration, d);
    const double maxReach = maxDuration * (maxRetimeFactor + Defaults::kOptimizerEpsilon);

    QVector<double> result;
    for (const double target : targetDurations) {
        if (target <= maxReach + Defaults::kOptimizerEpsilon) {
            result.append(target);
            continue;
        }
        const int pieces = int(qCeil(target / maxDuration));
        const double pieceTarget = target / pieces;
        for (int i = 0; i < pieces; ++i)
            result.append(pieceTarget);
    }
    return result;
}

OptimizedPlan quantizeDurations(const QVector<double> &targetDurations,
                                const QVector<double> &supportedDurations,
                                double maxRetimeFactor,
                                OptimizerError *error)
{
    OptimizedPlan plan;
    auto fail = [&](const QString &message) {
        if (error)
            *error = OptimizerError{message};
        return OptimizedPlan{};
    };

    if (error)
        *error = OptimizerError{};

    if (targetDurations.isEmpty())
        return fail(QStringLiteral("No scenes to optimize"));

    QVector<double> targets;
    targets.reserve(targetDurations.size());
    for (const double raw : targetDurations) {
        const double rounded = round(raw * 1000.0) / 1000.0;
        if (rounded <= 0.0)
            return fail(QStringLiteral("Scene target duration must be positive"));
        targets.append(rounded);
    }

    QVector<double> options = supportedDurations;
    if (options.isEmpty())
        return fail(QStringLiteral("No supported generation durations configured"));
    std::sort(options.begin(), options.end());

    // Per-scene feasible option indices.
    QVector<QVector<int>> allowed;
    allowed.reserve(targets.size());
    for (int i = 0; i < targets.size(); ++i) {
        QVector<int> feasible;
        for (int j = 0; j < options.size(); ++j)
            if (isFeasible(targets[i], options[j], maxRetimeFactor))
                feasible.append(j);
        if (feasible.isEmpty()) {
            return fail(QStringLiteral("No supported duration is feasible for target %1s "
                                       "(max_retime=%2)")
                            .arg(targets[i]).arg(maxRetimeFactor));
        }
        allowed.append(feasible);
    }

    const double step = granularity(options);
    const auto toUnits = [step](double seconds) {
        return int(qRound64(seconds / step));
    };

    int maxUnits = 0;
    for (const auto &feasible : allowed)
        maxUnits += toUnits(options[feasible.last()]);

    // dp[i][u] = minimal cost covering the first i scenes with u total units.
    QVector<QVector<double>> dp(targets.size() + 1,
                                QVector<double>(maxUnits + 1, kInfinity));
    QVector<QVector<int>> choice(targets.size() + 1, QVector<int>(maxUnits + 1, -1));
    dp[0][0] = 0.0;

    for (int i = 1; i <= targets.size(); ++i) {
        const double target = targets[i - 1];
        const QVector<int> &feasible = allowed[i - 1];

        // Lower bound on units reachable before scene i.
        int reachableBefore = 0;
        for (int k = 0; k < i - 1; ++k)
            reachableBefore += toUnits(options[allowed[k].first()]);

        for (int units = reachableBefore; units <= maxUnits; ++units) {
            if (dp[i - 1][units] == kInfinity)
                continue;
            for (const int optionIndex : feasible) {
                const int nextUnits = units + toUnits(options[optionIndex]);
                if (nextUnits > maxUnits)
                    continue;
                const double cost =
                    dp[i - 1][units] + qAbs(options[optionIndex] - target)
                    + Defaults::kClipCountPenalty;
                if (cost < dp[i][nextUnits] - 1e-12) {
                    dp[i][nextUnits] = cost;
                    choice[i][nextUnits] = optionIndex;
                }
            }
        }
    }

    int bestUnits = -1;
    double bestCost = kInfinity;
    for (int units = 0; units <= maxUnits; ++units) {
        if (dp[targets.size()][units] < bestCost - 1e-12) {
            bestCost = dp[targets.size()][units];
            bestUnits = units;
        }
    }
    if (bestUnits == -1) {
        return fail(QStringLiteral(
            "No generation combination satisfies the retime policy for every scene"));
    }

    QVector<double> generations;
    generations.resize(targets.size());
    int units = bestUnits;
    for (int i = targets.size(); i >= 1; --i) {
        const int optionIndex = choice[i][units];
        if (optionIndex < 0)
            return fail(QStringLiteral("Optimizer state corrupted during backtracking"));
        generations[i - 1] = options[optionIndex];
        units -= toUnits(options[optionIndex]);
    }

    double totalGeneration = 0.0;
    double totalError = 0.0;
    for (int i = 0; i < targets.size(); ++i) {
        totalGeneration += generations[i];
        totalError += qAbs(generations[i] - targets[i]);
    }

    plan.generationDurations = generations;
    plan.totalGenerationSeconds = round(totalGeneration * 1000.0) / 1000.0;
    plan.totalErrorSeconds = round(totalError * 1000.0) / 1000.0;
    return plan;
}

} // namespace TtvStudio::Render
