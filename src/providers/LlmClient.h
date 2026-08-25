#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <optional>

#include "ProviderError.h"
#include "Retry.h"

namespace TtvStudio::Providers {

class ITransport;

inline constexpr char kLlmProviderName[] = "openai_compatible_llm";

struct LlmConfig
{
    QString baseUrl; // e.g. https://api.vilao.ai/v1 (no trailing slash required)
    QString apiKey;  // optional; sent as Bearer when non-empty
    QString model;   // must be configured — empty → immediate Permanent error
    int timeoutMs = 120'000;
    int maxAttempts = 3;
};

struct LlmMessage
{
    QString role;    // "system" | "user" | "assistant"
    QString content;
};

struct LlmCompletionResult
{
    bool ok = false;
    QString content;                 // raw assistant text
    QJsonObject json;                // parsed object (completeStructured only)
    std::optional<int> promptTokens;
    std::optional<int> completionTokens;
    std::optional<int> totalTokens;
    double latencySeconds = 0.0;
    int rounds = 1;                  // completion rounds used (1 or 2 w/ repair)
    ProviderError error;

    static LlmCompletionResult failure(ProviderError err) { LlmCompletionResult r; r.error = std::move(err); return r; }
};

// Adapter for any OpenAI-compatible /chat/completions endpoint.
// Blocking; run on a worker thread. Transport and sleep are injectable for
// deterministic tests.
class LlmClient
{
public:
    LlmClient(ITransport &transport, LlmConfig config, SleepFn sleep = {});

    // Raw chat completion; retries Transient failures only.
    LlmCompletionResult complete(const QVector<LlmMessage> &messages, double temperature = 0.2);

    // Chat completion constrained to one JSON object validating against the
    // given JSON Schema string. One repair round-trip is attempted when the
    // first reply fails to parse; persistent malformed output is Permanent.
    LlmCompletionResult completeStructured(const QVector<LlmMessage> &messages,
                                           const QString &jsonSchema,
                                           double temperature = 0.2);

private:
    QString extractContentOrError(const QByteArray &body, ProviderError *err) const;

    ITransport &m_transport;
    LlmConfig m_config;
    SleepFn m_sleep;
};

} // namespace TtvStudio::Providers
