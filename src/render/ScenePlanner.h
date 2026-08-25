#pragma once

#include <optional>

#include <QString>
#include <QVector>

#include "providers/LlmClient.h"
#include "render/SceneManifest.h"

namespace TtvStudio::Render {

// Outcome of one planning call: semantic proposals plus usage metadata.
struct PlanOutcome
{
    bool ok = false;
    QVector<SceneProposal> proposals;
    Providers::LlmCompletionResult usage; // token/latency metadata from the LLM
    bool repairAttempted = false;
    QString error; // non-empty when !ok — fail-closed, never partial plans
};

// Asks the LLM for semantic scenes and enforces exact narration coverage
// deterministically. The LLM is only trusted to *split* the script; the
// coverage check guarantees no word is dropped, duplicated or rewritten.
class ScenePlanner
{
public:
    explicit ScenePlanner(Providers::LlmClient &client);

    // `durationHintSeconds` steers scene sizing; `language` names the
    // narration language in the prompt.
    PlanOutcome plan(const QString &script,
                     const QString &language,
                     std::optional<double> durationHintSeconds) const;

private:
    static QString systemPrompt();
    static QString userPrompt(const QString &script, const QString &language,
                              const QString &durationHint);
    static QString schemaJson();
    static QString repairUserPrompt(const QString &problem);

    // Parse {scenes:[{narration, visual_prompt, continuity}]} — tolerant of
    // markdown fencing (delegated to the LLM client's structured mode).
    static bool parseProposals(const QJsonObject &payload,
                               QVector<SceneProposal> *out);

    Providers::LlmClient &m_client;
};

} // namespace TtvStudio::Render
