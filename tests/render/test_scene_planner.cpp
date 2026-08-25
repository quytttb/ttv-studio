#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "../providers/FakeTransport.h"
#include "render/ScriptCoverage.h"
#include "render/ScenePlanner.h"

using namespace TtvStudio::Render;
using namespace TtvStudio::Providers;

namespace {

LlmConfig testConfig()
{
    LlmConfig config;
    config.baseUrl = QStringLiteral("https://llm.example/v1");
    config.model = QStringLiteral("planner-x");
    config.timeoutMs = 2'000;
    config.maxAttempts = 1; // planner-level repair is what we exercise
    return config;
}

HttpResponse okChatObject(const QJsonObject &payload)
{
    QJsonObject message{{QLatin1String("content"),
                         QString::fromUtf8(QJsonDocument(payload).toJson())}};
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.body = QJsonDocument(
                        QJsonObject{{QLatin1String("choices"), QJsonArray{QJsonObject{
                                        {QLatin1String("message"), message}}}}})
                        .toJson();
    return response;
}

HttpResponse scenesResponse(const QStringList &narrations, const QString &visualPrompt)
{
    QJsonArray scenes;
    for (const QString &n : narrations) {
        scenes.append(QJsonObject{{QLatin1String("narration"), n},
                                  {QLatin1String("visual_prompt"), visualPrompt}});
    }
    return okChatObject(QJsonObject{{QLatin1String("scenes"), scenes}});
}

} // namespace

class TestScenePlanner : public QObject
{
    Q_OBJECT

private slots:
    void exactCoveragePassesWithoutRepair()
    {
        FakeTransport transport;
        transport.script.append(scenesResponse({QStringLiteral("Một hai."),
                                                QStringLiteral("Ba bốn.")},
                                               QStringLiteral("a quiet street")));
        LlmClient client(transport, testConfig(), [](qint64) {});
        ScenePlanner planner{client};

        const auto outcome =
            planner.plan(QStringLiteral("Một hai. Ba bốn."), QStringLiteral("vi"), 8.0);

        QVERIFY(outcome.ok);
        QCOMPARE(outcome.proposals.size(), 2);
        QCOMPARE(outcome.proposals.first().visualPrompt, QStringLiteral("a quiet street"));
        QVERIFY(!outcome.repairAttempted);
        QCOMPARE(transport.calls.size(), 1);

        // Prompt carries the script and the duration hint.
        const QJsonObject body =
            QJsonDocument::fromJson(transport.calls.first().request.body).object();
        const QString userContent =
            body.value(QLatin1String("messages")).toArray().at(1).toObject().value(
                QLatin1String("content")).toString();
        QVERIFY(userContent.contains(QStringLiteral("Một hai. Ba bốn.")));
        QVERIFY(userContent.contains(QLatin1String("8.00 seconds")));
    }

    void whitespaceOnlyDifferencesStillPass()
    {
        FakeTransport transport;
        // The LLM re-wrapped lines but kept every word in order.
        transport.script.append(scenesResponse({QStringLiteral("Một   hai."),
                                                QStringLiteral("Ba\nbốn.")},
                                               QStringLiteral("shot")));
        LlmClient client(transport, testConfig(), [](qint64) {});
        ScenePlanner planner{client};

        const auto outcome =
            planner.plan(QStringLiteral("Một hai. Ba bốn."), QStringLiteral("vi"), {});
        QVERIFY(outcome.ok);
    }

    void coverageFailureTriggersRepairRound()
    {
        FakeTransport transport;
        // First attempt drops the second sentence → coverage violation.
        transport.script.append(scenesResponse({QStringLiteral("Một hai.")},
                                               QStringLiteral("shot")));
        // Repair reproduces the full script.
        transport.script.append(scenesResponse({QStringLiteral("Một hai."),
                                                QStringLiteral("Ba bốn.")},
                                               QStringLiteral("shot")));
        LlmClient client(transport, testConfig(), [](qint64) {});
        ScenePlanner planner{client};

        const auto outcome =
            planner.plan(QStringLiteral("Một hai. Ba bốn."), QStringLiteral("vi"), {});

        QVERIFY(outcome.ok);
        QVERIFY(outcome.repairAttempted);
        QCOMPARE(transport.calls.size(), 2);
        QCOMPARE(outcome.proposals.size(), 2);

        // The repair prompt names the violation.
        const QJsonObject repairBody =
            QJsonDocument::fromJson(transport.calls.at(1).request.body).object();
        const auto messages = repairBody.value(QLatin1String("messages")).toArray();
        QVERIFY(messages.last().toObject().value(QLatin1String("content"))
                    .toString()
                    .contains(QLatin1String("coverage rules")));
    }

    void persistentBadCoverageFailsClosed()
    {
        FakeTransport transport;
        transport.script.append(scenesResponse({QStringLiteral("Một hai.")},
                                               QStringLiteral("shot")));
        transport.script.append(scenesResponse({QStringLiteral("Hoàn toàn khác.")},
                                               QStringLiteral("shot")));
        LlmClient client(transport, testConfig(), [](qint64) {});
        ScenePlanner planner{client};

        const auto outcome =
            planner.plan(QStringLiteral("Một hai. Ba bốn."), QStringLiteral("vi"), {});

        QVERIFY(!outcome.ok);
        QVERIFY(outcome.error.contains(QLatin1String("coverage failed after repair")));
    }

    void emptyScriptFailsImmediately()
    {
        FakeTransport transport;
        LlmClient client(transport, testConfig(), [](qint64) {});
        ScenePlanner planner{client};

        const auto outcome = planner.plan(QStringLiteral("   "), QStringLiteral("vi"), {});
        QVERIFY(!outcome.ok);
        QVERIFY(outcome.error.contains(QLatin1String("empty after normalization")));
        QCOMPARE(transport.calls.size(), 0);
    }
};

QTEST_MAIN(TestScenePlanner)
#include "test_scene_planner.moc"
