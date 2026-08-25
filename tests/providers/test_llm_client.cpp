#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "FakeTransport.h"
#include "providers/LlmClient.h"

using namespace TtvStudio::Providers;

namespace {

LlmConfig testConfig()
{
    LlmConfig config;
    config.baseUrl = QStringLiteral("https://api.example.ai/v1");
    config.apiKey = QStringLiteral("sk-secret-key-123");
    config.model = QStringLiteral("test-model");
    config.timeoutMs = 5'000;
    config.maxAttempts = 3;
    return config;
}

HttpResponse okChat(const QString &content, const QJsonObject &usage = {})
{
    QJsonObject message{{QLatin1String("content"), content}};
    QJsonObject choice{{QLatin1String("message"), message}};
    QJsonObject payload{
        {QLatin1String("choices"), QJsonArray{choice}},
        {QLatin1String("usage"), usage},
    };
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return response;
}

HttpResponse httpError(int statusCode, const QByteArray &body = {})
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = statusCode;
    response.body = body;
    return response;
}

} // namespace

class TestLlmClient : public QObject
{
    Q_OBJECT

private slots:
    void emptyModelFailsClosedWithoutCalling()
    {
        FakeTransport transport;
        LlmConfig config = testConfig();
        config.model.clear();
        LlmClient client(transport, config, [](qint64) {});

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}});
        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(transport.calls.size(), 0);
    }

    void successParsesContentAndUsage()
    {
        FakeTransport transport;
        transport.script.append(okChat(QStringLiteral("{\"answer\": 42}"),
                                       QJsonObject{{QLatin1String("prompt_tokens"), 11},
                                                   {QLatin1String("completion_tokens"), 7},
                                                   {QLatin1String("total_tokens"), 18}}));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result = client.complete(
            {{QStringLiteral("system"), QStringLiteral("be brief")},
             {QStringLiteral("user"), QStringLiteral("question")}});

        QVERIFY(result.ok);
        QCOMPARE(result.content, QStringLiteral("{\"answer\": 42}"));
        QCOMPARE(result.promptTokens.value_or(-1), 11);
        QCOMPARE(result.completionTokens.value_or(-1), 7);
        QCOMPARE(result.totalTokens.value_or(-1), 18);
        QVERIFY(result.error.message.isEmpty());

        // Request shape: endpoint, auth header, JSON body fields.
        QCOMPARE(transport.calls.size(), 1);
        const auto &call = transport.calls.first();
        QCOMPARE(call.request.url.toString(),
                 QStringLiteral("https://api.example.ai/v1/chat/completions"));
        QCOMPARE(call.request.headerValue(QStringLiteral("Authorization")),
                 QStringLiteral("Bearer sk-secret-key-123"));
        const QJsonObject body =
            QJsonDocument::fromJson(call.request.body).object();
        QCOMPARE(body.value(QLatin1String("model")).toString(), QStringLiteral("test-model"));
        QCOMPARE(int(body.value(QLatin1String("messages")).toArray().size()), 2);
        QCOMPARE(body.value(QLatin1String("response_format"))
                     .toObject()
                     .value(QLatin1String("type"))
                     .toString(),
                 QStringLiteral("json_object"));
    }

    void transientStatusRetriesThenSucceeds()
    {
        FakeTransport transport;
        transport.script.append(httpError(503));
        transport.script.append(httpError(429));
        transport.script.append(okChat(QStringLiteral("fine")));
        int sleeps = 0;
        LlmClient client(transport, testConfig(), [&](qint64) { ++sleeps; });

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}}, 0.2);

        QVERIFY(result.ok);
        QCOMPARE(transport.calls.size(), 3);
        QCOMPARE(sleeps, 2);
    }

    void permanentStatusDoesNotRetry()
    {
        FakeTransport transport;
        transport.script.append(httpError(401, "{\"error\":\"bad key\"}"));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}});

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(result.error.statusCode, 401);
        QVERIFY(result.error.message.contains(QLatin1String("HTTP 401")));
        QCOMPARE(transport.calls.size(), 1); // no retry on permanent failure
    }

    void apiKeyLeakIsRedactedInErrors()
    {
        FakeTransport transport;
        transport.script.append(httpError(403, "denied for sk-secret-key-123"));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}});

        QVERIFY(!result.ok);
        QVERIFY(!result.error.message.contains(QLatin1String("sk-secret-key-123")));
    }

    void timeoutIsTransientAndRetried()
    {
        FakeTransport transport;
        HttpResponse timeoutResponse;
        timeoutResponse.timedOut = true;
        timeoutResponse.errorText = QStringLiteral("request timed out");
        transport.script.append(timeoutResponse);
        transport.script.append(okChat(QStringLiteral("late but ok")));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}});

        QVERIFY(result.ok);
        QCOMPARE(transport.calls.size(), 2);
    }

    void structuredParsesFencedJsonDirectly()
    {
        FakeTransport transport;
        transport.script.append(
            okChat(QStringLiteral("```json\n{\"segments\": []}\n```")));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result =
            client.completeStructured({{QStringLiteral("user"), QStringLiteral("plan")}},
                                      QStringLiteral("{\"type\": \"object\"}"));

        QVERIFY(result.ok);
        QCOMPARE(result.rounds, 1);
        QCOMPARE(int(result.json.value(QLatin1String("segments")).toArray().size()), 0);
        // System instruction with the schema was injected.
        const QJsonObject body =
            QJsonDocument::fromJson(transport.calls.first().request.body).object();
        const QString systemContent =
            body.value(QLatin1String("messages")).toArray().at(0).toObject().value(
                QLatin1String("content")).toString();
        QVERIFY(systemContent.startsWith(QLatin1String("Respond with a single JSON object")));
        QVERIFY(systemContent.contains(QLatin1String("\"type\": \"object\"")));
    }

    void structuredRepairsBrokenOutputOnce()
    {
        FakeTransport transport;
        transport.script.append(okChat(QStringLiteral("this is prose, not json")));
        transport.script.append(okChat(QStringLiteral("{\"fixed\": true}")));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result =
            client.completeStructured({{QStringLiteral("user"), QStringLiteral("plan")}},
                                      QStringLiteral("{\"type\": \"object\"}"));

        QVERIFY(result.ok);
        QCOMPARE(result.rounds, 2);
        QVERIFY(result.json.contains(QLatin1String("fixed")));

        // Repair round appended the broken assistant reply + correction prompt.
        QCOMPARE(transport.calls.size(), 2);
        const QJsonObject repairBody =
            QJsonDocument::fromJson(transport.calls.at(1).request.body).object();
        const auto messages = repairBody.value(QLatin1String("messages")).toArray();
        QCOMPARE(messages.last().toObject().value(QLatin1String("role")).toString(),
                 QStringLiteral("user"));
        QVERIFY(messages.last().toObject().value(QLatin1String("content"))
                    .toString()
                    .contains(QLatin1String("Reply again with the corrected single JSON object")));
    }

    void persistentMalformedOutputIsPermanent()
    {
        FakeTransport transport;
        transport.script.append(okChat(QStringLiteral("still not json")));
        transport.script.append(okChat(QStringLiteral("nope")));
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result =
            client.completeStructured({{QStringLiteral("user"), QStringLiteral("plan")}},
                                      QStringLiteral("{\"type\": \"object\"}"));

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QVERIFY(result.error.message.contains(QLatin1String("after repair")));
    }

    void missingChoicesIsPermanent()
    {
        FakeTransport transport;
        HttpResponse bogus;
        bogus.networkOk = true;
        bogus.statusCode = 200;
        bogus.body = QByteArrayLiteral("{\"unrelated\": true}");
        transport.script.append(bogus);
        LlmClient client(transport, testConfig(), [](qint64) {});

        const auto result = client.complete({{QStringLiteral("user"), QStringLiteral("hi")}});

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QVERIFY(result.error.message.contains(QLatin1String("choices[0]")));
    }
};

QTEST_MAIN(TestLlmClient)
#include "test_llm_client.moc"
