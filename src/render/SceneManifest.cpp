#include "SceneManifest.h"

#include <QtMath>

#include "ScriptCoverage.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Render {

namespace {

// Rounding per-scene targets to 3 decimals drifts up to 0.5 ms each; allow a
// small accumulated tolerance when checking against the narration length.
constexpr double kTotalDurationToleranceS = 0.01;

// Weight narration time across scenes by per-scene text length, merging any
// sub-minimum target into its predecessor so no scene is too short.
QVector<double> provisionalTargets(const QVector<SceneProposal> &proposals,
                                   double totalDurationSeconds,
                                   SceneManifestError *error)
{
    if (proposals.isEmpty()) {
        if (error)
            *error = SceneManifestError{QStringLiteral("No scene proposals to time")};
        return {};
    }
    if (totalDurationSeconds <= 0.0) {
        if (error)
            *error = SceneManifestError{QStringLiteral("Total duration must be positive")};
        return {};
    }

    double totalWeight = 0.0;
    QVector<double> weights;
    weights.reserve(proposals.size());
    for (const SceneProposal &proposal : proposals) {
        const double weight = qMax(qsizetype(proposal.narration.size()), qsizetype(1));
        weights.append(double(weight));
        totalWeight += weight;
    }

    QVector<std::pair<double, int>> merged; // (target, proposal index)
    for (int i = 0; i < proposals.size(); ++i) {
        const double target = totalDurationSeconds * weights[i] / totalWeight;
        if (!merged.empty() && merged.last().first < Defaults::kMinSceneSeconds)
            merged.last().first += target; // extend previous, keep its proposal
        else
            merged.append({target, i});
    }
    // Fold a trailing sliver into the previous scene.
    if (merged.size() > 1 && merged.last().first < Defaults::kMinSceneSeconds) {
        merged[merged.size() - 2].first += merged.last().first;
        merged.removeLast();
    }

    QVector<double> targets;
    targets.reserve(merged.size());
    for (const auto &[target, index] : merged)
        targets.append(target);
    return targets;
}

} // namespace

ScenePlan buildScenePlan(const QVector<SceneProposal> &proposals,
                         double totalDurationSeconds,
                         const QVector<double> &supportedGenerationDurations,
                         double maxRetimeFactor,
                         SceneManifestError *error)
{
    if (error)
        error->message.clear();

    QVector<double> rawTargets =
        provisionalTargets(proposals, totalDurationSeconds, error);
    if (error && !error->message.isEmpty())
        return {};

    // Split over-reach targets into balanced sub-scenes sharing one proposal.
    QVector<std::pair<double, int>> expanded; // (target, proposal index)
    for (int i = 0; i < rawTargets.size(); ++i) {
        QVector<double> pieces = splitLongTargets({rawTargets[i]},
                                                  supportedGenerationDurations,
                                                  maxRetimeFactor);
        for (const double piece : pieces)
            expanded.append({piece, i});
    }
    if (expanded.isEmpty()) {
        if (error)
            *error = SceneManifestError{QStringLiteral("Scene split produced no targets")};
        return {};
    }

    QVector<double> targets;
    targets.reserve(expanded.size());
    for (const auto &[target, index] : expanded)
        targets.append(round(target * 1000.0) / 1000.0);

    OptimizerError optimizerError;
    const OptimizedPlan optimized =
        quantizeDurations(targets, supportedGenerationDurations, maxRetimeFactor,
                          &optimizerError);
    if (!optimizerError.message.isEmpty()) {
        if (error)
            *error = SceneManifestError{optimizerError.message};
        return {};
    }
    if (optimized.generationDurations.size() != expanded.size()) {
        if (error) {
            *error = SceneManifestError{
                QStringLiteral("Optimizer returned a different scene count than the "
                               "narrative split")};
        }
        return {};
    }

    ScenePlan plan;
    double cursor = 0.0;
    for (int i = 0; i < expanded.size(); ++i) {
        const auto &[target, proposalIndex] = expanded[i];
        const SceneProposal &proposal = proposals[proposalIndex];

        Scene scene;
        scene.id = QStringLiteral("scene_%1").arg(i + 1, 3, 10, QChar('0'));
        scene.index = i + 1;
        scene.narration = proposal.narration;
        scene.startSeconds = round(cursor * 1000.0) / 1000.0;
        cursor += targets[i];
        scene.endSeconds = round(cursor * 1000.0) / 1000.0;
        scene.targetDurationSeconds = targets[i];
        scene.generationDurationSeconds = optimized.generationDurations[i];
        scene.visualPrompt = proposal.visualPrompt;
        scene.continuity = proposal.continuity;
        scene.status = SceneStatus::Planned;
        plan.scenes.append(scene);
    }
    plan.totalDurationSeconds = round(cursor * 1000.0) / 1000.0;

    const QVector<CoverageProblem> problems =
        validatePlan(plan, totalDurationSeconds, maxRetimeFactor);
    if (!problems.isEmpty()) {
        QStringList messages;
        for (const CoverageProblem &problem : problems)
            messages.append(problem.message);
        if (error)
            *error = SceneManifestError{messages.join(QStringLiteral("; "))};
        return {};
    }
    return plan;
}

QVector<CoverageProblem> validatePlan(const ScenePlan &plan,
                                      double expectedTotalSeconds,
                                      double maxRetimeFactor)
{
    QVector<CoverageProblem> problems;
    double cursor = 0.0;
    for (int i = 0; i < plan.scenes.size(); ++i) {
        const Scene &scene = plan.scenes.at(i);
        const int expectedIndex = i + 1;
        if (scene.index != expectedIndex) {
            problems.append({expectedIndex,
                             QStringLiteral("scene index out of order: expected %1, got %2")
                                 .arg(expectedIndex).arg(scene.index)});
        }
        if (qAbs(scene.startSeconds - cursor) > 1e-3) {
            problems.append({expectedIndex,
                             QStringLiteral("scene %1: gap/overlap at start (expected %2, got %3)")
                                 .arg(expectedIndex).arg(cursor, 0, 'f', 3)
                                 .arg(scene.startSeconds, 0, 'f', 3)});
        }
        if (scene.endSeconds <= scene.startSeconds) {
            problems.append({expectedIndex,
                             QStringLiteral("scene %1: non-positive duration").arg(expectedIndex)});
        }
        if (scene.generationDurationSeconds <= 0.0) {
            problems.append(
                {expectedIndex,
                 QStringLiteral("scene %1: invalid generation duration").arg(expectedIndex)});
        } else {
            // The generated clip must cover its window by trimming or a mild
            // stretch within policy.
            const bool feasible =
                scene.generationDurationSeconds >= scene.targetDurationSeconds - 1e-6
                || scene.generationDurationSeconds
                       >= scene.targetDurationSeconds / (maxRetimeFactor + 1e-6);
            if (!feasible) {
                problems.append(
                    {expectedIndex,
                     QStringLiteral("scene %1: generation %2s cannot cover target %3s "
                                    "(retime policy %4)")
                         .arg(expectedIndex)
                         .arg(scene.generationDurationSeconds, 0, 'f', 3)
                         .arg(scene.targetDurationSeconds, 0, 'f', 3)
                         .arg(maxRetimeFactor, 0, 'f', 2)});
            }
        }
        cursor += scene.targetDurationSeconds;
        cursor = round(cursor * 1000.0) / 1000.0;
    }

    if (qAbs(plan.totalDurationSeconds - cursor) > 1e-3) {
        problems.append({0, QStringLiteral("plan total %1s != timeline end %2s")
                                .arg(plan.totalDurationSeconds, 0, 'f', 3)
                                .arg(cursor, 0, 'f', 3)});
    }
    if (expectedTotalSeconds > 0.0
        && qAbs(cursor - expectedTotalSeconds) > kTotalDurationToleranceS) {
        problems.append({0, QStringLiteral("timeline covers %1s but narration lasts %2s")
                                .arg(cursor, 0, 'f', 3)
                                .arg(expectedTotalSeconds, 0, 'f', 3)});
    }
    return problems;
}

} // namespace TtvStudio::Render
