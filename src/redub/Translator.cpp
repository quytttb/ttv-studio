#include "Translator.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "utils/AppConstants.h"

namespace TtvStudio::Redub {

using Providers::LlmCompletionResult;
using Providers::LlmMessage;

namespace {

constexpr double kMinSegmentDurationS = 0.2;

QVector<TranscriptSegment> chunkOf(const Transcript &transcript, int start, int size)
{
    QVector<TranscriptSegment> chunk;
    const int end = qMin(start + size, transcript.segments.size());
    for (int i = start; i < end; ++i)
        chunk.append(transcript.segments.at(i));
    return chunk;
}

} // namespace

TranscriptTranslator::TranscriptTranslator(Providers::LlmClient &client,
                                           TranslatorConfig config)
    : m_client(client),
      m_config(std::move(config))
{
    if (m_config.batchSize <= 0)
        m_config.batchSize = Defaults::kTranslationBatchSize;
}

QString TranscriptTranslator::systemPrompt() const
{
    const QString sourceNote = m_config.sourceLanguage.isEmpty()
                                   ? QStringLiteral("Detect the source language from the text.")
                                   : QStringLiteral("The source audio language is %1.")
                                         .arg(m_config.sourceLanguage);
    return QStringLiteral(
               "You are a professional subtitle and dubbing translator. %1 Translate each "
               "numbered segment into %2. Rules:\n"
               "- Translate meaning faithfully with natural spoken phrasing.\n"
               "- Respect the per-segment target character length so the dub fits the "
               "original timing; stay within the given min/max character range.\n"
               "- Keep proper nouns, numbers and technical terms accurate.\n"
               "- Never merge or split segments; return exactly one entry per index.")
        .arg(sourceNote, m_config.targetLanguage);
}

QString TranscriptTranslator::batchUserPrompt(const QVector<TranscriptSegment> &chunk,
                                              double charsPerSecond)
{
    QJsonArray payload;
    for (const TranscriptSegment &segment : chunk) {
        const double duration = qMax(segment.durationSeconds(), kMinSegmentDurationS);
        const int targetChars = int(qRound(duration * charsPerSecond));
        payload.append(QJsonObject{
            {QLatin1String("index"), segment.index},
            {QLatin1String("text"), segment.text},
            {QLatin1String("target_chars"), targetChars},
            {QLatin1String("min_chars"),
             int(targetChars * Defaults::kDubLengthFloorRatio)},
            {QLatin1String("max_chars"),
             int(targetChars * Defaults::kDubLengthCeilRatio)},
        });
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QLatin1String("segments"), payload}})
            .toJson(QJsonDocument::Compact));
}

bool TranscriptTranslator::parseBatch(const QJsonObject &payload,
                                      const QVector<int> &expectedIndexes,
                                      QHash<int, QString> *out)
{
    out->clear();
    const auto translations = payload.value(QLatin1String("translations")).toArray();
    for (const auto &v : translations) {
        const QJsonObject o = v.toObject();
        const QJsonValue idx = o.value(QLatin1String("index"));
        const QString text = o.value(QLatin1String("text")).toString().trimmed();
        if (!idx.isDouble() || text.isEmpty())
            return false;
        const int index = int(idx.toDouble());
        if (out->contains(index))
            return false; // duplicate
        out->insert(index, text);
    }

    if (out->size() != expectedIndexes.size())
        return false;
    for (const int expected : expectedIndexes) {
        if (!out->contains(expected))
            return false; // dropped index
    }
    return true;
}

bool TranscriptTranslator::translate(const Transcript &transcript, Translation *out,
                                     TranslationError *error) const
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = TranslationError{message};
        return false;
    };
    if (error)
        error->message.clear();

    if (transcript.segments.isEmpty())
        return fail(QStringLiteral("transcript has no segments"));

    QHash<int, QString> collected;
    collected.reserve(transcript.segments.size());

    for (int start = 0; start < transcript.segments.size(); start += m_config.batchSize) {
        const QVector<TranscriptSegment> chunk =
            chunkOf(transcript, start, m_config.batchSize);
        QVector<int> expected;
        expected.reserve(chunk.size());
        for (const TranscriptSegment &s : chunk)
            expected.append(s.index);

        const QVector<LlmMessage> messages{
            {QStringLiteral("system"), systemPrompt()},
            {QStringLiteral("user"), batchUserPrompt(chunk, m_config.charsPerSecond)},
        };

        LlmCompletionResult result = m_client.completeStructured(messages, QStringLiteral(R"({
  "type": "object",
  "properties": {
    "translations": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "index": {"type": "integer"},
          "text": {"type": "string"}
        },
        "required": ["index", "text"]
      }
    }
  },
  "required": ["translations"]
})"),
                                                    /*temperature*/ 0.2);
        if (!result.ok)
            return fail(result.error.message);

        QHash<int, QString> batch;
        if (!parseBatch(result.json, expected, &batch)) {
            // One deterministic retry at temperature 0.
            result = m_client.completeStructured(messages, QStringLiteral(R"({
  "type": "object",
  "properties": {
    "translations": {
      "type": "array",
      "items": {
        "type": "object",
        "properties": {
          "index": {"type": "integer"},
          "text": {"type": "string"}
        },
        "required": ["index", "text"]
      }
    }
  },
  "required": ["translations"]
})"),
                                                    /*temperature*/ 0.0);
            if (!result.ok)
                return fail(result.error.message);
            if (!parseBatch(result.json, expected, &batch)) {
                return fail(QStringLiteral("translation batch %1–%2 failed twice")
                                .arg(expected.first())
                                .arg(expected.last()));
            }
        }

        for (auto it = batch.constBegin(); it != batch.constEnd(); ++it)
            collected.insert(it.key(), it.value());
    }

    out->targetLanguage = m_config.targetLanguage;
    out->sourceLanguage = transcript.language.isEmpty() ? m_config.sourceLanguage
                                                        : transcript.language;
    out->segments.clear();
    out->segments.reserve(collected.size());
    for (const TranscriptSegment &segment : std::as_const(transcript.segments)) {
        TranslatedSegment translated;
        translated.index = segment.index;
        translated.text = collected.value(segment.index);
        out->segments.append(translated);
    }
    return true;
}

} // namespace TtvStudio::Redub
