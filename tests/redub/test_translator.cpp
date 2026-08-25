#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

#include "../providers/FakeTransport.h"
#include "redub/Translator.h"

using namespace TtvStudio::Redub;
using namespace TtvStudio::Providers;

namespace {

LlmConfig llmConfig()
{
    LlmConfig config;
    config.baseUrl = QStringLiteral("https://llm.local/v1");
    config.model = QStringLiteral("translator-x");
    config.timeoutMs = 2'000;
    config.maxAttempts = 1;
    return config;
}

HttpResponse translationsResponse(const QList<QPair<int, QString>> &items)
{
    QJsonArray arr;
    for (const auto &[index, text] : items)
        arr.append(QJsonObject{{QLatin1String("index"), index},
                               {QLatin1String("text"), text}});
    QJsonObject contentPayload{{QLatin1String("translations"), arr}};
    QJsonObject choice{{QLatin1String("message"),
                        QJsonObject{{QLatin1String("content"),
                                     QString::fromUtf8(
                                         QJsonDocument(contentPayload).toJson())}}}};
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.body = QJsonDocument(
                        QJsonObject{{QLatin1String("choices"), QJsonArray{choice}}})
                        .toJson();
    return response;
}

Transcript sampleTranscript(int segments)
{
    Transcript t;
    t.language = QStringLiteral("zh");
    double cursor = 0.0;
    for (int i = 1; i <= segments; ++i) {
        t.segments.append({i, cursor, cursor + 2.0,
                           QStringLiteral("segment %1 text").arg(i)});
        cursor += 2.0;
    }
    return t;
}

} // namespace

class TestTranslator : public QObject
{
    Q_OBJECT

private slots:
    void translatesSingleBatchWithDurationBudgets()
    {
        FakeTransport transport;
        transport.script.append(translationsResponse(
            {{1, QStringLiteral("Xin chào")}, {2, QStringLiteral("Nói về AI.")}}));
        LlmClient client(transport, llmConfig(), [](qint64) {});
        TranscriptTranslator translator{client};

        Translation out;
        TranslationError error;
        QVERIFY(translator.translate(sampleTranscript(2), &out, &error));
        QVERIFY(error.message.isEmpty());
        QCOMPARE(out.targetLanguage, QStringLiteral("vi"));
        QCOMPARE(out.segments.size(), 2);
        QCOMPARE(out.segments.first().text, QStringLiteral("Xin chào"));

        // Batch prompt carried the per-segment character budgets.
        const QJsonObject body =
            QJsonDocument::fromJson(transport.calls.at(0).request.body).object();
        const QString userContent =
            body.value(QLatin1String("messages")).toArray().at(1).toObject().value(
                QLatin1String("content")).toString();
        const QJsonObject payload =
            QJsonDocument::fromJson(userContent.toUtf8()).object();
        const auto segments = payload.value(QLatin1String("segments")).toArray();
        QCOMPARE(segments.size(), 2);
        // 2s window at 14 chars/s → target 28; floor 16 (0.6), ceil 39 (1.4).
        QCOMPARE(segments.at(0).toObject().value(QLatin1String("target_chars")).toInt(),
                 28);
        QCOMPARE(segments.at(0).toObject().value(QLatin1String("min_chars")).toInt(),
                 16);
        QCOMPARE(segments.at(0).toObject().value(QLatin1String("max_chars")).toInt(),
                 39);
    }

    void batchesLargeTranscripts()
    {
        FakeTransport transport;
        // 25 segments with batch size 10 → 3 calls.
        for (int batch = 0; batch < 3; ++batch) {
            QList<QPair<int, QString>> items;
            const int first = batch * 10 + 1;
            const int last = qMin(batch * 10 + 10, 25);
            for (int i = first; i <= last; ++i)
                items.append({i, QStringLiteral("bản dịch %1").arg(i)});
            transport.script.append(translationsResponse(items));
        }
        LlmClient client(transport, llmConfig(), [](qint64) {});
        TranslatorConfig config;
        config.batchSize = 10;
        TranscriptTranslator translator{client, config};

        Translation out;
        TranslationError error;
        QVERIFY(translator.translate(sampleTranscript(25), &out, &error));
        QCOMPARE(out.segments.size(), 25);
        QCOMPARE(out.segments.last().text, QStringLiteral("bản dịch 25"));
        QCOMPARE(transport.calls.size(), 3);
    }

    void malformedBatchRetriesOnceAtDeterministicTemperature()
    {
        FakeTransport transport;
        // First reply drops index 2 → validation failure.
        transport.script.append(translationsResponse({{1, QStringLiteral("chỉ một")}}));
        // Retry returns the complete set.
        transport.script.append(translationsResponse(
            {{1, QStringLiteral("một")}, {2, QStringLiteral("hai")}}));
        LlmClient client(transport, llmConfig(), [](qint64) {});
        TranscriptTranslator translator{client};

        Translation out;
        TranslationError error;
        QVERIFY(translator.translate(sampleTranscript(2), &out, &error));
        QVERIFY(error.message.isEmpty());
        QCOMPARE(out.segments.at(1).text, QStringLiteral("hai"));

        // The retry request must carry temperature 0.
        const QJsonObject retryBody =
            QJsonDocument::fromJson(transport.calls.at(1).request.body).object();
        QCOMPARE(retryBody.value(QLatin1String("temperature")).toDouble(), 0.0);
    }

    void persistentMalformedBatchFailsClosed()
    {
        FakeTransport transport;
        transport.script.append(translationsResponse({{1, QStringLiteral("x")}}));
        transport.script.append(translationsResponse({{9, QStringLiteral("y")}})); // wrong set
        LlmClient client(transport, llmConfig(), [](qint64) {});
        TranscriptTranslator translator{client};

        Translation out;
        TranslationError error;
        QVERIFY(!translator.translate(sampleTranscript(2), &out, &error));
        QVERIFY(error.message.contains(QLatin1String("failed twice")));
    }

    void emptyTranscriptFailsImmediately()
    {
        FakeTransport transport;
        LlmClient client(transport, llmConfig(), [](qint64) {});
        TranscriptTranslator translator{client};

        Translation out;
        TranslationError error;
        QVERIFY(!translator.translate(Transcript{}, &out, &error));
        QVERIFY(error.message.contains(QLatin1String("no segments")));
        QCOMPARE(transport.calls.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestTranslator)
#include "test_translator.moc"
