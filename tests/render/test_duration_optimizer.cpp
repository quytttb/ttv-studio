#include <QtMath>
#include <QtTest>

#include "render/DurationOptimizer.h"

using namespace TtvStudio::Render;

class TestDurationOptimizer : public QObject
{
    Q_OBJECT

private slots:
    void trimmingIsAlwaysFeasibleStretchingIsPolicyBounded()
    {
        QVERIFY(isFeasible(4.0, 6.0, 1.10)); // trim
        QVERIFY(isFeasible(4.0, 3.8, 1.10)); // stretch 1.0526 ≤ 1.10
        QVERIFY(!isFeasible(4.0, 3.0, 1.10)); // stretch 1.333 > policy
    }

    void retimeFactorComputation()
    {
        QCOMPARE(retimeFactor(5.0, 4.0), 1.25);
    }

    void splitLongTargetsBalancesPieces()
    {
        // Target 20s with max clip 8s and retime 1.1 → reach 8.8 → 3 pieces.
        const auto pieces = splitLongTargets({20.0}, {4.0, 6.0, 8.0}, 1.10);
        QCOMPARE(pieces.size(), 3);
        for (const double piece : pieces)
            QCOMPARE(piece, 20.0 / 3);

        // Within reach → untouched.
        const auto single = splitLongTargets({8.0}, {4.0, 6.0, 8.0}, 1.10);
        QCOMPARE(single.size(), 1);
    }

    void quantizePrefersClosestDurations()
    {
        OptimizerError error;
        const auto plan = quantizeDurations({4.2, 7.9}, {4.0, 6.0, 8.0}, 1.10, &error);
        QVERIFY(error.message.isEmpty());
        QCOMPARE(plan.generationDurations.size(), 2);
        // 4.2 → stretch from 4 (factor 1.05 feasible) beats trimming 6.
        // 7.9 → trim from 8 beats stretching 6 (factor 1.317 > policy).
        QCOMPARE(plan.generationDurations.at(0), 4.0);
        QCOMPARE(plan.generationDurations.at(1), 8.0);
        QVERIFY(plan.totalErrorSeconds > 0.0);
    }

    void quantizeIsDeterministic()
    {
        // Post-split targets: every value must be coverable by {4,6,8} under
        // the 1.10 retime policy (that is what buildScenePlan guarantees).
        OptimizerError e1, e2;
        const QVector<double> targets{5.5, 6.5, 7.0, 7.5};
        const auto a = quantizeDurations(targets, {4.0, 6.0, 8.0}, 1.10, &e1);
        const auto b = quantizeDurations(targets, {4.0, 6.0, 8.0}, 1.10, &e2);
        QVERIFY(e1.message.isEmpty());
        QVERIFY(e2.message.isEmpty());
        QCOMPARE(a.generationDurations, b.generationDurations);
        QCOMPARE(a.totalGenerationSeconds, b.totalGenerationSeconds);
    }

    void infeasibleTargetFailsClosed()
    {
        OptimizerError error;
        (void)quantizeDurations({30.0}, {4.0, 6.0, 8.0}, 1.10, &error);
        QVERIFY(!error.message.isEmpty());
        QVERIFY(error.message.contains(QLatin1String("feasible")));
    }

    void emptyInputAndBadOptionsFail()
    {
        OptimizerError error;
        (void)quantizeDurations({}, {4.0}, 1.10, &error);
        QVERIFY(!error.message.isEmpty());

        error = OptimizerError{};
        (void)quantizeDurations({4.0}, {}, 1.10, &error);
        QVERIFY(!error.message.isEmpty());
    }
};

QTEST_MAIN(TestDurationOptimizer)
#include "test_duration_optimizer.moc"
