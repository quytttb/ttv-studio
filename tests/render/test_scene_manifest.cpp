#include <QtTest>

#include "render/SceneManifest.h"

using namespace TtvStudio::Render;

namespace {

QVector<SceneProposal> sampleProposals()
{
    SceneProposal a;
    a.narration = QStringLiteral("Câu mở đầu khá dài để chiếm thời lượng lớn hơn.");
    a.visualPrompt = QStringLiteral("sunrise over a quiet village, wide shot");

    SceneProposal b;
    b.narration = QStringLiteral("Nhịp giữa.");
    b.visualPrompt = QStringLiteral("close-up of hands typing on a keyboard");
    b.continuity.location = QStringLiteral("studio");

    SceneProposal c;
    c.narration = QStringLiteral("Kết thúc bằng một lời hứa về tương lai rộng mở.");
    c.visualPrompt = QStringLiteral("child running through a field at golden hour");

    return {a, b, c};
}

} // namespace

class TestSceneManifest : public QObject
{
    Q_OBJECT

private slots:
    void buildsTimedPlanProportionally()
    {
        const auto proposals = sampleProposals();
        const double total = 18.0;

        SceneManifestError error;
        const ScenePlan plan =
            buildScenePlan(proposals, total, {4.0, 6.0, 8.0}, 1.10, &error);

        QVERIFY(error.message.isEmpty());
        QCOMPARE(plan.scenes.size(), 3);
        // Timeline is contiguous from zero.
        QVERIFY(qAbs(plan.scenes.at(0).startSeconds) < 1e-9);
        QVERIFY(plan.scenes.at(0).endSeconds <= plan.scenes.at(1).startSeconds + 1e-9);
        QVERIFY(plan.scenes.at(1).endSeconds <= plan.scenes.at(2).startSeconds + 1e-9);
        // Per-scene targets round to ms; allow the accumulated drift.
        QVERIFY(qAbs(plan.totalDurationSeconds - total) < 0.01);
        // Generation durations are drawn from the supported set.
        for (const Scene &scene : std::as_const(plan.scenes)) {
            QVERIFY(scene.generationDurationSeconds == 4.0
                    || scene.generationDurationSeconds == 6.0
                    || scene.generationDurationSeconds == 8.0);
            QVERIFY(!scene.id.isEmpty());
        }
        QCOMPARE(plan.scenes.first().id, QStringLiteral("scene_001"));
    }

    void idsAndIndicesAreSequential()
    {
        QVector<SceneProposal> proposals;
        for (int i = 0; i < 5; ++i) {
            SceneProposal p;
            p.narration = QStringLiteral("Đoạn văn số %1 với nội dung đủ dài.").arg(i + 1);
            p.visualPrompt = QStringLiteral("abstract visual %1").arg(i + 1);
            proposals.append(p);
        }

        SceneManifestError error;
        const ScenePlan plan = buildScenePlan(proposals, 20.0, {4.0, 6.0}, 1.10, &error);
        QVERIFY(error.message.isEmpty());
        for (int i = 0; i < plan.scenes.size(); ++i)
            QCOMPARE(plan.scenes.at(i).index, i + 1);
    }

    void jsonRoundTripPreservesPlan()
    {
        SceneManifestError error;
        const ScenePlan original =
            buildScenePlan(sampleProposals(), 15.0, {4.0, 6.0, 8.0}, 1.10, &error);
        QVERIFY(error.message.isEmpty());

        Scene scene = original.scenes.first();
        scene.status = SceneStatus::ClipReady;
        scene.providerTaskId = QStringLiteral("task-123");
        ScenePlan mutated = original;
        mutated.scenes[0] = scene;

        ScenePlan restored;
        QVERIFY(ScenePlan::fromJson(mutated.toJson(), &restored));
        QCOMPARE(restored.scenes.size(), mutated.scenes.size());
        QCOMPARE(restored.totalDurationSeconds, mutated.totalDurationSeconds);

        const Scene &s = restored.scenes.first();
        QCOMPARE(s.id, scene.id);
        QCOMPARE(s.index, scene.index);
        QCOMPARE(s.narration, scene.narration);
        QCOMPARE(s.startSeconds, scene.startSeconds);
        QCOMPARE(s.endSeconds, scene.endSeconds);
        QCOMPARE(s.targetDurationSeconds, scene.targetDurationSeconds);
        QCOMPARE(s.generationDurationSeconds, scene.generationDurationSeconds);
        QCOMPARE(s.visualPrompt, scene.visualPrompt);
        QCOMPARE(s.providerTaskId, scene.providerTaskId);
        QCOMPARE(int(s.status), int(SceneStatus::ClipReady));
        QCOMPARE(s.continuity.location, scene.continuity.location);
        QCOMPARE(s.continuity.characters, scene.continuity.characters);
    }

    void malformedJsonFailsClosed()
    {
        ScenePlan plan;
        QJsonObject bogus{{QLatin1String("scenes"), 42}};
        QVERIFY(!ScenePlan::fromJson(bogus, &plan));

        QJsonObject badId{{QLatin1String("id"), QStringLiteral("not-an-id")}};
        Scene scene;
        QVERIFY(!Scene::fromJson(badId, &scene));

        QJsonObject negativeDuration{
            {QLatin1String("id"), QStringLiteral("scene_001")},
            {QLatin1String("index"), 1},
            {QLatin1String("narration"), QStringLiteral("text")},
            {QLatin1String("start_seconds"), 0.0},
            {QLatin1String("end_seconds"), -3.0},
            {QLatin1String("target_duration_seconds"), 4.0},
            {QLatin1String("generation_duration_seconds"), 6.0},
            {QLatin1String("visual_prompt"), QStringLiteral("a shot")},
        };
        QVERIFY(!Scene::fromJson(negativeDuration, &scene));
    }

    void validateDetectsGapsAndBadRetime()
    {
        SceneManifestError error;
        ScenePlan plan = buildScenePlan(sampleProposals(), 12.0, {4.0, 6.0, 8.0}, 1.10, &error);
        QVERIFY(error.message.isEmpty());

        QVERIFY(validatePlan(plan, 12.0, 1.10).isEmpty());

        // Break contiguity.
        plan.scenes[1].startSeconds += 1.0;
        const auto gapProblems = validatePlan(plan, 12.0, 1.10);
        QVERIFY(!gapProblems.isEmpty());
        QVERIFY(gapProblems.first().message.contains(QLatin1String("gap/overlap")));

        // Restore, then make generation infeasible (clip shorter than policy allows).
        plan.scenes[1].startSeconds -= 1.0;
        plan.scenes[2].generationDurationSeconds = 1.0; // target ~4s → factor 4x
        const auto retimeProblems = validatePlan(plan, 12.0, 1.10);
        bool foundRetimeProblem = false;
        for (const auto &p : retimeProblems)
            foundRetimeProblem |= p.message.contains(QLatin1String("cannot cover"));
        QVERIFY(foundRetimeProblem);
    }

    void emptyProposalsFailClosed()
    {
        SceneManifestError error;
        (void)buildScenePlan({}, 12.0, {4.0, 6.0}, 1.10, &error);
        QVERIFY(!error.message.isEmpty());
    }
};

QTEST_MAIN(TestSceneManifest)
#include "test_scene_manifest.moc"
