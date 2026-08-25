#pragma once

#include <QString>

#include "providers/LlmClient.h"
#include "redub/Transcript.h"

namespace TtvStudio::Redub {

// Tunables mirroring the redub translation contract.
struct TranslatorConfig
{
    double charsPerSecond = 14.0;   // spoken pacing → target character budget
    int batchSize = 10;             // transcript segments per LLM call
    QString targetLanguage = QStringLiteral("vi");
    QString sourceLanguage;         // empty → let the model detect
};

struct TranslationError
{
    QString message;
};

// Duration-aware translator: converts the timestamped transcript into
// target-language narration whose length fits each original window, so the
// dub can follow the original clock without heavy retiming.
//
// Batching: segments are translated in batches; every batch must return
// exactly one translation per requested index (no merges/splits/drops).
// A malformed batch is retried once at temperature 0, then fails closed.
class TranscriptTranslator
{
public:
    explicit TranscriptTranslator(Providers::LlmClient &client, TranslatorConfig config = {});

    // Translates `transcript` fully; `error` is set on any failure — callers
    // must fail closed and never assemble a partial dub.
    bool translate(const Transcript &transcript, Translation *out, TranslationError *error) const;

private:
    QString systemPrompt() const;
    static QString batchUserPrompt(const QVector<TranscriptSegment> &chunk,
                                   double charsPerSecond);
    static bool parseBatch(const QJsonObject &payload, const QVector<int> &expectedIndexes,
                           QHash<int, QString> *out);

    Providers::LlmClient &m_client;
    TranslatorConfig m_config;
};

} // namespace TtvStudio::Redub
