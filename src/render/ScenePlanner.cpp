#include "ScenePlanner.h"

#include <QJsonArray>

#include "ScriptCoverage.h"

namespace TtvStudio::Render {

using Providers::LlmClient;
using Providers::LlmCompletionResult;
using Providers::LlmMessage;

namespace {

} // namespace

ScenePlanner::ScenePlanner(LlmClient &client)
    : m_client(client)
{
}

QString ScenePlanner::systemPrompt()
{
    return QStringLiteral(
        "You are a scene planner for automated video production.\n"
        "\n"
        "You split one narration script into contiguous semantic scenes for AI video\n"
        "generation. Rules:\n"
        "\n"
        "1. Every scene's narration text must be an exact, contiguous excerpt of the\n"
        "   original script. Never rewrite, translate, drop, duplicate or reorder any\n"
        "   words or punctuation.\n"
        "2. Split at natural semantic boundaries: topic changes, sentence endings,\n"
        "   subject or location changes. Prefer 3-10 seconds of narration per scene.\n"
        "3. Each visual_prompt describes only what is visible on screen, in one vivid\n"
        "   concrete shot, suitable for a text-to-video model. No narration quotes.\n"
        "4. Carry continuity context (characters, location, style) across adjacent\n"
        "   scenes so consecutive clips look consistent.\n"
        "5. Reply in the language of the script for narration; visual prompts in English.");
}

QString ScenePlanner::userPrompt(const QString &script, const QString &language,
                                 const QString &durationHint)
{
    return QStringLiteral(
               "Script (narration language: %1):\n"
               "\n"
               "<<<SCRIPT_START>>>\n"
               "%2\n"
               "<<<SCRIPT_END>>>\n"
               "\n"
               "Total narration duration: %3.\n"
               "\n"
               "Decompose the script into semantic scenes following the rules. The\n"
               "concatenation of all scene narration texts must reproduce the script exactly.")
        .arg(language, script, durationHint);
}

QString ScenePlanner::schemaJson()
{
    return QStringLiteral(R"({
  "type": "object",
  "properties": {
    "scenes": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "properties": {
          "narration": {"type": "string", "minLength": 1},
          "visual_prompt": {"type": "string", "minLength": 1},
          "continuity": {
            "type": "object",
            "properties": {
              "characters": {"type": "array", "items": {"type": "string"}},
              "location": {"type": ["string", "null"]},
              "style": {"type": ["string", "null"]}
            }
          }
        },
        "required": ["narration", "visual_prompt"]
      }
    }
  },
  "required": ["scenes"]
})");
}

QString ScenePlanner::repairUserPrompt(const QString &problem)
{
    return QStringLiteral(
               "Your previous scene decomposition violated the coverage rules:\n%1\n\n"
               "Fix it: the concatenation of scene narration texts must reproduce the script "
               "exactly, in order, without drops, duplicates or rewrites.")
        .arg(problem);
}

bool ScenePlanner::parseProposals(const QJsonObject &payload, QVector<SceneProposal> *out)
{
    out->clear();
    const auto scenes = payload.value(QLatin1String("scenes")).toArray();
    if (scenes.isEmpty())
        return false;

    for (const auto &value : scenes) {
        const QJsonObject obj = value.toObject();
        SceneProposal proposal;
        proposal.narration = obj.value(QLatin1String("narration")).toString().trimmed();
        proposal.visualPrompt = obj.value(QLatin1String("visual_prompt")).toString().trimmed();
        if (proposal.narration.isEmpty() || proposal.visualPrompt.isEmpty())
            return false;
        const QJsonValue continuity = obj.value(QLatin1String("continuity"));
        if (continuity.isObject())
            proposal.continuity = ContinuityContext::fromJson(continuity.toObject());
        out->append(proposal);
    }
    return !out->isEmpty();
}

PlanOutcome ScenePlanner::plan(const QString &script, const QString &language,
                               std::optional<double> durationHintSeconds) const
{
    PlanOutcome outcome;

    const QString normalized = normalizeScript(script);
    if (normalized.isEmpty()) {
        outcome.error = QStringLiteral("Script is empty after normalization");
        return outcome;
    }

    const QString durationHint = durationHintSeconds.has_value()
                                     ? QStringLiteral("%1 seconds").arg(*durationHintSeconds, 0, 'f', 2)
                                     : QStringLiteral("unknown");

    QVector<LlmMessage> messages{
        {QStringLiteral("system"), systemPrompt()},
        {QStringLiteral("user"), userPrompt(normalized, language, durationHint)},
    };

    LlmCompletionResult result =
        m_client.completeStructured(messages, schemaJson(), /*temperature*/ 0.2);

    QVector<SceneProposal> proposals;
    if (result.ok && parseProposals(result.json, &proposals)) {
        // Coverage is the real gate — an LLM reply that parses but rewrites
        // the script is still rejected.
        const QStringList narrations = [&proposals] {
            QStringList list;
            for (const SceneProposal &p : proposals)
                list.append(p.narration);
            return list;
        }();
        const auto problems = verifyCoverage(normalizedKey(normalized), narrations);
        if (problems.isEmpty()) {
            outcome.ok = true;
            outcome.proposals = proposals;
            outcome.usage = result;
            return outcome;
        }
        outcome.repairAttempted = true;
        messages.append({QStringLiteral("assistant"), result.content});
        messages.append({QStringLiteral("user"), repairUserPrompt(problems.first().message)});
        result = m_client.completeStructured(messages, schemaJson(), 0.2);
        if (result.ok && parseProposals(result.json, &proposals)) {
            QStringList retryNarrations;
            for (const SceneProposal &p : proposals)
                retryNarrations.append(p.narration);
            const auto retryProblems = verifyCoverage(normalizedKey(normalized), retryNarrations);
            if (retryProblems.isEmpty()) {
                outcome.ok = true;
                outcome.proposals = proposals;
                outcome.usage = result;
                return outcome;
            }
            outcome.error = QStringLiteral("coverage failed after repair: %1")
                                .arg(retryProblems.first().message);
            outcome.usage = result;
            return outcome;
        }
        if (!result.ok) {
            outcome.error = result.error.message;
            outcome.usage = result;
            return outcome;
        }
        outcome.error = QStringLiteral("coverage failed after repair: malformed scenes payload");
        outcome.usage = result;
        return outcome;
    }

    if (!result.ok) {
        outcome.error = result.error.message;
        outcome.usage = result;
        return outcome;
    }
    outcome.error = QStringLiteral("malformed scenes payload");
    outcome.usage = result;
    return outcome;
}

} // namespace TtvStudio::Render
