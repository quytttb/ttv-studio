#include "ScriptCoverage.h"

#include <QRegularExpression>

namespace TtvStudio::Render {

namespace {

const QRegularExpression &whitespacePattern()
{
    static const QRegularExpression pattern(QStringLiteral("\\s+"));
    return pattern;
}

} // namespace

QString normalizeScript(const QString &script)
{
    QString collapsed = script;
    collapsed.replace(QChar(0x00A0), QChar(' ')); // NBSP → space
    collapsed.replace(whitespacePattern(), QStringLiteral(" "));
    return collapsed.trimmed();
}

QString normalizedKey(const QString &text)
{
    QString key = text;
    key.remove(whitespacePattern());
    return key;
}

QVector<CoverageProblem> verifyCoverage(const QString &scriptKey,
                                        const QStringList &sceneNarrations)
{
    QVector<CoverageProblem> problems;

    qsizetype cursor = 0;
    int index = 0;
    for (const QString &narration : sceneNarrations) {
        ++index;
        const QString segment = normalizedKey(narration);
        if (segment.isEmpty()) {
            problems.append({index, QStringLiteral("scene %1: empty narration").arg(index)});
            return problems;
        }
        if (cursor + segment.size() > scriptKey.size()
            || QStringView(scriptKey).mid(cursor, segment.size()) != segment) {
            problems.append(
                {index,
                 QStringLiteral("scene %1: narration is not a contiguous in-order excerpt "
                                "of the script")
                     .arg(index)});
            return problems;
        }
        cursor += segment.size();
    }

    if (cursor != scriptKey.size()) {
        problems.append(
            {0, QStringLiteral("narration text dropped (%1 chars of the script are uncovered)")
                    .arg(scriptKey.size() - cursor)});
    }
    return problems;
}

} // namespace TtvStudio::Render
