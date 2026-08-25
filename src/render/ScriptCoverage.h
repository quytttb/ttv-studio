#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace TtvStudio::Render {

struct CoverageProblem
{
    int sceneIndex = 0;   // 1-based; 0 = plan-level problem
    QString message;
};

// Collapse whitespace (incl. NBSP) and trim a raw script into canonical
// narration text.
QString normalizeScript(const QString &script);

// Whitespace-insensitive key used to verify narration coverage: all
// whitespace removed. Concatenation of scene keys must equal the script key
// exactly — this makes drops, duplicates, rewrites and reordering detectable
// deterministically, without trusting the LLM.
QString normalizedKey(const QString &text);

// Verifies that the per-scene narration keys concatenate back into `scriptKey`
// exactly, in order. Returns an empty vector when coverage is exact.
QVector<CoverageProblem> verifyCoverage(const QString &scriptKey,
                                        const QStringList &sceneNarrations);

} // namespace TtvStudio::Render
