#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "DurationOptimizer.h"
#include "SceneTypes.h"
#include "ScriptCoverage.h"

namespace TtvStudio::Render {

// One semantic scene proposed by the LLM before deterministic timing.
struct SceneProposal
{
    QString narration;
    QString visualPrompt;
    ContinuityContext continuity;
};

struct SceneManifestError
{
    QString message;
};

// Deterministic plan builder: weights narration time across scenes by text
// length, merges sub-minimum scenes, splits over-reach targets and quantizes
// generation durations. `error->message` non-empty means failure — the caller
// must fail closed.
ScenePlan buildScenePlan(const QVector<SceneProposal> &proposals,
                         double totalDurationSeconds,
                         const QVector<double> &supportedGenerationDurations,
                         double maxRetimeFactor,
                         SceneManifestError *error);

// Fail-closed checks: contiguous timeline (no gaps/overlaps), ordered 1-based
// indices, positive durations, generation durations feasible per retime
// policy. Returns an empty vector when the plan is sound.
QVector<CoverageProblem> validatePlan(const ScenePlan &plan,
                                      double expectedTotalSeconds,
                                      double maxRetimeFactor);

} // namespace TtvStudio::Render
