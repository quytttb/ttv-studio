#include "Transcript.h"

namespace TtvStudio::Redub {

QJsonObject TranscriptSegment::toJson() const
{
    return QJsonObject{
        {QLatin1String("index"), index},
        {QLatin1String("start_seconds"), startSeconds},
        {QLatin1String("end_seconds"), endSeconds},
        {QLatin1String("text"), text},
    };
}

bool TranscriptSegment::fromJson(const QJsonObject &obj, TranscriptSegment *out)
{
    *out = TranscriptSegment{};
    const QJsonValue idx = obj.value(QLatin1String("index"));
    const QJsonValue start = obj.value(QLatin1String("start_seconds"));
    const QJsonValue end = obj.value(QLatin1String("end_seconds"));
    if (!idx.isDouble() || !start.isDouble() || !end.isDouble())
        return false;

    out->index = int(idx.toDouble());
    out->startSeconds = start.toDouble();
    out->endSeconds = end.toDouble();
    out->text = obj.value(QLatin1String("text")).toString().trimmed();

    return out->index >= 1 && out->endSeconds > out->startSeconds && !out->text.isEmpty();
}

QJsonObject Transcript::toJson() const
{
    QJsonArray arr;
    for (const TranscriptSegment &s : segments)
        arr.append(s.toJson());
    return QJsonObject{
        {QLatin1String("language"), language},
        {QLatin1String("provider"), provider},
        {QLatin1String("model"), model},
        {QLatin1String("segments"), arr},
    };
}

bool Transcript::fromJson(const QJsonObject &obj, Transcript *out)
{
    *out = Transcript{};
    out->language = obj.value(QLatin1String("language")).toString();
    out->provider = obj.value(QLatin1String("provider")).toString();
    out->model = obj.value(QLatin1String("model")).toString();

    const auto arr = obj.value(QLatin1String("segments")).toArray();
    if (arr.isEmpty())
        return false;

    out->segments.reserve(arr.size());
    double previousStart = -1.0;
    for (int i = 0; i < arr.size(); ++i) {
        TranscriptSegment segment;
        if (!arr.at(i).isObject() || !TranscriptSegment::fromJson(arr.at(i).toObject(), &segment))
            return false;
        // Contiguous 1-based ordering, non-decreasing timeline.
        if (segment.index != i + 1)
            return false;
        if (previousStart >= 0.0 && segment.startSeconds < previousStart - 1e-9)
            return false;
        previousStart = segment.startSeconds;
        out->segments.append(segment);
    }
    return true;
}

QJsonObject Translation::toJson() const
{
    QJsonArray arr;
    for (const TranslatedSegment &s : segments)
        arr.append(QJsonObject{{QLatin1String("index"), s.index},
                               {QLatin1String("text"), s.text}});
    return QJsonObject{
        {QLatin1String("target_language"), targetLanguage},
        {QLatin1String("source_language"), sourceLanguage},
        {QLatin1String("segments"), arr},
    };
}

bool Translation::fromJson(const QJsonObject &obj, Translation *out)
{
    *out = Translation{};
    out->targetLanguage = obj.value(QLatin1String("target_language")).toString().trimmed();
    if (out->targetLanguage.isEmpty())
        return false;
    out->sourceLanguage = obj.value(QLatin1String("source_language")).toString();

    const auto arr = obj.value(QLatin1String("segments")).toArray();
    if (arr.isEmpty())
        return false;

    out->segments.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        TranslatedSegment segment;
        segment.text = o.value(QLatin1String("text")).toString().trimmed();
        const QJsonValue idx = o.value(QLatin1String("index"));
        if (!idx.isDouble() || segment.text.isEmpty())
            return false;
        segment.index = int(idx.toDouble());
        out->segments.append(segment);
    }
    return true;
}

} // namespace TtvStudio::Redub
