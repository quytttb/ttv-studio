#include "DubPlanner.h"

#include "utils/AppConstants.h"

namespace TtvStudio::Redub {

bool planDubTiming(const QVector<double> &windowSeconds,
                   const QVector<int> &segmentIndexes,
                   const QVector<double> &narrationSeconds,
                   double minRate,
                   double maxRate,
                   QVector<DubTiming> *out,
                   DubPlanError *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = DubPlanError{message};
        return false;
    };
    if (error)
        error->message.clear();

    if (windowSeconds.isEmpty())
        return fail(QStringLiteral("no transcript windows to plan"));
    if (segmentIndexes.size() != windowSeconds.size()
        || narrationSeconds.size() != windowSeconds.size()) {
        return fail(QStringLiteral("transcript/translation/narration size mismatch"));
    }

    out->clear();
    out->reserve(windowSeconds.size());
    for (int i = 0; i < windowSeconds.size(); ++i) {
        const double window = windowSeconds.at(i);
        const double narration = narrationSeconds.at(i);
        if (window <= Defaults::kDubWindowEpsilonS)
            return fail(QStringLiteral("segment %1: degenerate window").arg(
                segmentIndexes.at(i)));
        if (narration <= 0.0)
            return fail(QStringLiteral("segment %1: missing narration duration")
                            .arg(segmentIndexes.at(i)));

        DubTiming timing;
        timing.segmentIndex = segmentIndexes.at(i);
        timing.windowSeconds = window;
        timing.narrationSeconds = narration;

        const double rawRate = narration / window;
        timing.atempoRate = qBound(minRate, rawRate, maxRate);
        // fitsExactly means no clamping happened.
        timing.fitsExactly = qAbs(timing.atempoRate - rawRate) < 1e-3;
        out->append(timing);
    }
    return true;
}

} // namespace TtvStudio::Redub
