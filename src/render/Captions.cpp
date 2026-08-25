#include "Captions.h"

#include <QRegularExpression>

namespace TtvStudio::Render {

namespace {

constexpr int kMaxCharsPerCue = 96;

const QRegularExpression &sentenceSplitPattern()
{
    // Split after .!?… followed by whitespace.
    static const QRegularExpression pattern(
        QStringLiteral("(?<=[.!?…])\\s+"));
    return pattern;
}

QString formatTimestamp(double seconds)
{
    qint64 millis = qint64(qRound64(seconds * 1000.0));
    const qint64 hours = millis / 3'600'000;
    millis %= 3'600'000;
    const qint64 minutes = millis / 60'000;
    millis %= 60'000;
    const qint64 secs = millis / 1000;
    millis %= 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

} // namespace

QStringList splitSentences(const QString &script)
{
    QStringList sentences;
    const QString trimmed = script.trimmed();
    if (trimmed.isEmpty())
        return sentences;

    const auto parts = trimmed.split(sentenceSplitPattern(), Qt::SkipEmptyParts);
    for (QString partRaw : parts) {
        const QString part = partRaw.trimmed();
        if (part.isEmpty())
            continue;
        if (part.size() <= kMaxCharsPerCue) {
            sentences.append(part);
            continue;
        }
        // Wrap long sentences on word boundaries.
        const QStringList words = part.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
        QStringList current;
        qsizetype length = 0;
        for (const QString &word : words) {
            const qsizetype added = current.isEmpty() ? word.size() : word.size() + 1;
            if (!current.isEmpty() && length + added > kMaxCharsPerCue) {
                sentences.append(current.join(QChar(' ')));
                current.clear();
                length = 0;
            }
            current.append(word);
            length += added;
        }
        if (!current.isEmpty())
            sentences.append(current.join(QChar(' ')));
    }
    return sentences;
}

QVector<CaptionCue> proportionalCues(const QString &script, double totalDurationSeconds)
{
    const QStringList sentences = splitSentences(script);
    QVector<CaptionCue> cues;
    if (sentences.isEmpty() || totalDurationSeconds <= 0.0)
        return cues;

    double totalWeight = 0.0;
    QVector<double> weights;
    weights.reserve(sentences.size());
    for (const QString &sentence : sentences) {
        const double weight = qMax<qsizetype>(sentence.size(), 1);
        weights.append(weight);
        totalWeight += weight;
    }

    double cursor = 0.0;
    for (int i = 0; i < sentences.size(); ++i) {
        const bool last = i == sentences.size() - 1;
        const double end =
            last ? totalDurationSeconds
                 : cursor + totalDurationSeconds * weights[i] / totalWeight;
        CaptionCue cue;
        cue.index = i + 1;
        cue.startSeconds = round(cursor * 1000.0) / 1000.0;
        cue.endSeconds = round(end * 1000.0) / 1000.0;
        cue.text = sentences[i];
        cues.append(cue);
        cursor = end;
    }
    return cues;
}

QVector<CaptionCue> sceneCaptions(const QVector<Scene> &scenes)
{
    QVector<CaptionCue> cues;
    cues.reserve(scenes.size());
    for (const Scene &scene : scenes) {
        CaptionCue cue;
        cue.index = scene.index;
        cue.startSeconds = scene.startSeconds;
        cue.endSeconds = scene.endSeconds;
        cue.text = scene.narration;
        cues.append(cue);
    }
    return cues;
}

QString renderVtt(const QVector<CaptionCue> &cues)
{
    QStringList lines{QStringLiteral("WEBVTT"), QString()};
    for (const CaptionCue &cue : cues) {
        lines.append(QString::number(cue.index));
        lines.append(QStringLiteral("%1 --> %2")
                         .arg(formatTimestamp(cue.startSeconds),
                              formatTimestamp(cue.endSeconds)));
        lines.append(cue.text);
        lines.append(QString());
    }
    return lines.join(QChar('\n'));
}

} // namespace TtvStudio::Render
