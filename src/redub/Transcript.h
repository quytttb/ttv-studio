#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace TtvStudio::Redub {

// One timestamped utterance recognized from a source video.
struct TranscriptSegment
{
    int index = 0; // 1-based
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    QString text;

    double durationSeconds() const { return endSeconds - startSeconds; }

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &obj, TranscriptSegment *out);
};

// Timestamped transcription of one source video; durable redub artifact.
// Segments must be ordered (1-based contiguous indices).
struct Transcript
{
    QString language; // may be empty when unknown
    QString provider;
    QString model;
    QVector<TranscriptSegment> segments;

    QJsonObject toJson() const;
    // Returns false on malformed input — callers must fail closed and
    // re-transcribe rather than trust a partial artifact.
    static bool fromJson(const QJsonObject &obj, Transcript *out);
};

// One translated utterance keyed by the transcript segment index.
struct TranslatedSegment
{
    int index = 0;
    QString text;
};

struct Translation
{
    QString targetLanguage;
    QString sourceLanguage;
    QVector<TranslatedSegment> segments;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &obj, Translation *out);
};

} // namespace TtvStudio::Redub
