#include <QtTest>

#include "redub/DubPlanner.h"

using namespace TtvStudio::Redub;

// Mirror the policy band under test (AppConstants values).
static constexpr double kMinRate = 0.85;
static constexpr double kMaxRate = 1.25;

class TestDubPlanner : public QObject
{
    Q_OBJECT

private slots:
    void computesRatesInsideTheBand()
    {
        QVector<double> windows{2.0, 4.0, 1.0};
        QVector<int> indexes{1, 2, 3};
        QVector<double> narration{2.0, 4.4, 1.0}; // rates: 1.0, 1.10, 1.0

        QVector<DubTiming> out;
        DubPlanError error;
        QVERIFY(planDubTiming(windows, indexes, narration,
                              kMinRate, kMaxRate, &out, &error));
        QVERIFY(error.message.isEmpty());
        QCOMPARE(out.size(), 3);
        QCOMPARE(out.at(0).atempoRate, 1.0);
        QVERIFY(qAbs(out.at(1).atempoRate - 1.10) < 1e-9);
        QVERIFY(out.at(1).fitsExactly);
    }

    void clampsToPolicyAndMarksSpill()
    {
        // Narration far longer than the window → clamp to maxRate.
        QVector<double> windows{2.0};
        QVector<int> indexes{1};
        QVector<double> narration{3.5}; // raw rate 1.75 > 1.25

        QVector<DubTiming> out;
        DubPlanError error;
        QVERIFY(planDubTiming(windows, indexes, narration,
                              kMinRate, kMaxRate, &out,
                              &error));
        QCOMPARE(out.first().atempoRate, kMaxRate);
        QVERIFY(!out.first().fitsExactly); // spill-over flagged for the UI

        // Far shorter narration → floor at minRate.
        narration = {1.0}; // raw rate 0.5 < 0.85
        QVERIFY(planDubTiming(windows, indexes, narration,
                              kMinRate, kMaxRate, &out,
                              &error));
        QCOMPARE(out.first().atempoRate, kMinRate);
        QVERIFY(!out.first().fitsExactly);
    }

    void sizeMismatchFailsClosed()
    {
        DubPlanError error;
        QVector<DubTiming> out;
        QVERIFY(!planDubTiming({2.0}, {1}, {}, kMinRate,
                               kMaxRate, &out, &error));
        QVERIFY(error.message.contains(QLatin1String("mismatch")));

        error = {};
        QVERIFY(!planDubTiming({}, {}, {}, kMinRate,
                               kMaxRate, &out, &error));
        QVERIFY(error.message.contains(QLatin1String("no transcript")));
    }

    void degenerateInputsFailClosed()
    {
        DubPlanError error;
        QVector<DubTiming> out;
        // Zero-length window.
        QVERIFY(!planDubTiming({0.0}, {1}, {1.0}, kMinRate,
                               kMaxRate, &out, &error));
        QVERIFY(!error.message.isEmpty());

        // Missing narration duration.
        error = {};
        QVERIFY(!planDubTiming({2.0}, {1}, {0.0}, kMinRate,
                               kMaxRate, &out, &error));
        QVERIFY(error.message.contains(QLatin1String("missing narration")));
    }
};

QTEST_GUILESS_MAIN(TestDubPlanner)
#include "test_dub_planner.moc"
