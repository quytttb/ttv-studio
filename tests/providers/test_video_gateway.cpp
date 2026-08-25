#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "FakeTransport.h"
#include "providers/VideoGatewayClient.h"

using namespace TtvStudio::Providers;

namespace {

HttpResponse okJson(const QJsonObject &payload)
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return response;
}

HttpResponse httpStatus(int statusCode)
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = statusCode;
    return response;
}

VideoSubmitRequest sampleRequest()
{
    VideoSubmitRequest request;
    request.prompt = QStringLiteral("a cat surfing a wave");
    request.aspectRatio = QStringLiteral("16:9");
    request.mode = QStringLiteral("t2v");
    request.resolution = QStringLiteral("1080p");
    request.durationSeconds = 8;
    return request;
}

} // namespace

class TestVideoGateway : public QObject
{
    Q_OBJECT

private slots:
    void emptyModelFailsClosed()
    {
        FakeTransport transport;
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"),
                                  QStringLiteral("key"), QString());

        const auto result = client.submit(sampleRequest());
        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(transport.calls.size(), 0);
    }

    void submitSendsBodyAndApiKeyHeader()
    {
        FakeTransport transport;
        transport.script.append(okJson(QJsonObject{{QLatin1String("task_id"), 12345},
                                                   {QLatin1String("status"),
                                                    QStringLiteral("pending")}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765/"),
                                  QStringLiteral("gw-key-1"), QStringLiteral("veo-3"));

        const auto result = client.submit(sampleRequest());

        QVERIFY(result.ok);
        QCOMPARE(result.taskId, QStringLiteral("12345"));
        QCOMPARE(result.rawStatus, QStringLiteral("pending"));

        const auto &call = transport.calls.first();
        QCOMPARE(call.request.url.toString(),
                 QStringLiteral("http://127.0.0.1:8765/api/video/generate"));
        QCOMPARE(call.request.headerValue(QStringLiteral("X-API-Key")),
                 QStringLiteral("gw-key-1"));
        const QJsonObject body = QJsonDocument::fromJson(call.request.body).object();
        QCOMPARE(body.value(QLatin1String("prompt")).toString(),
                 QStringLiteral("a cat surfing a wave"));
        QCOMPARE(body.value(QLatin1String("model")).toString(), QStringLiteral("veo-3"));
        QCOMPARE(body.value(QLatin1String("video_length")).toInt(), 8);
        QCOMPARE(int(body.value(QLatin1String("resolution")).toArray().size()), 1);
    }

    void submitTimeoutIsAmbiguousNotTransient()
    {
        FakeTransport transport;
        HttpResponse timeoutResponse;
        timeoutResponse.timedOut = true;
        timeoutResponse.errorText = QStringLiteral("timed out");
        transport.script.append(timeoutResponse);
        int sleeps = 0;
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"),
                                  QString(), QStringLiteral("veo-3"), 1'000, 3,
                                  [&](qint64) { ++sleeps; });

        const auto result = client.submit(sampleRequest());

        QVERIFY(!result.ok);
        // Ambiguous → no retry, the gateway may already be rendering.
        QCOMPARE(result.error.kind, ErrorKind::AmbiguousTimeout);
        QCOMPARE(transport.calls.size(), 1);
        QCOMPARE(sleeps, 0);
    }

    void submitRetriesTransientStatuses()
    {
        FakeTransport transport;
        transport.script.append(httpStatus(503));
        transport.script.append(
            okJson(QJsonObject{{QLatin1String("id"), QStringLiteral("abc")},
                               {QLatin1String("poll_url"), QStringLiteral("/api/status/abc")}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("veo-3"), 1'000, 3, [](qint64) {});

        const auto result = client.submit(sampleRequest());

        QVERIFY(result.ok);
        QCOMPARE(result.taskId, QStringLiteral("abc"));   // falls back to "id"
        QCOMPARE(result.pollUrl, QStringLiteral("/api/status/abc"));
        QCOMPARE(transport.calls.size(), 2);
    }

    void submitPermanentStopsImmediately()
    {
        FakeTransport transport;
        transport.script.append(httpStatus(401));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("veo-3"), 1'000, 3, [](qint64) {});

        const auto result = client.submit(sampleRequest());

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(result.error.statusCode, 401);
        QCOMPARE(transport.calls.size(), 1);
    }

    void submitMissingTaskIdIsPermanent()
    {
        FakeTransport transport;
        transport.script.append(okJson(QJsonObject{{QLatin1String("status"),
                                                    QStringLiteral("pending")}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("veo-3"), 1'000, 3, [](qint64) {});

        const auto result = client.submit(sampleRequest());

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QVERIFY(result.error.message.contains(QLatin1String("task_id")));
    }

    void pollMapsRawStatuses()
    {
        struct Row
        {
            QByteArray rawStatus;
            VideoTaskState expected;
        };
        const QList<Row> rows{
            {QByteArrayLiteral("queued"), VideoTaskState::Submitted},
            {QByteArrayLiteral("running"), VideoTaskState::Running},
            {QByteArrayLiteral("processing"), VideoTaskState::Running},
            {QByteArrayLiteral("completed"), VideoTaskState::Succeeded},
            {QByteArrayLiteral("done"), VideoTaskState::Succeeded},
        };

        for (const auto &row : rows) {
            FakeTransport transport;
            transport.script.append(okJson(QJsonObject{
                {QLatin1String("status"), QString::fromUtf8(row.rawStatus)}}));
            VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"),
                                      QString(), QStringLiteral("m"), 1'000, 1, [](qint64) {});
            const auto result = client.poll(QStringLiteral("tid"));
            QVERIFY2(result.ok, row.rawStatus.constData());
            QCOMPARE(result.state, row.expected);
        }
    }

    void pollFailureClassification()
    {
        // error_code == 0 → retryable
        FakeTransport transport;
        transport.script.append(okJson(QJsonObject{
            {QLatin1String("status"), QStringLiteral("failed")},
            {QLatin1String("error_code"), 0}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("m"), 1'000, 1, [](qint64) {});
        auto result = client.poll(QStringLiteral("tid"));
        QVERIFY(result.ok);
        QCOMPARE(result.state, VideoTaskState::FailedRetryable);

        // "timeout" message → retryable
        FakeTransport transport2;
        transport2.script.append(okJson(QJsonObject{
            {QLatin1String("status"), QStringLiteral("failed")},
            {QLatin1String("error_code"), 7},
            {QLatin1String("error"), QStringLiteral("generation timeout reached")}}));
        VideoGatewayClient client2(transport2, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                   QStringLiteral("m"), 1'000, 1, [](qint64) {});
        result = client2.poll(QStringLiteral("tid"));
        QVERIFY(result.ok);
        QCOMPARE(result.state, VideoTaskState::FailedRetryable);

        // anything else → permanent
        FakeTransport transport3;
        transport3.script.append(okJson(QJsonObject{
            {QLatin1String("status"), QStringLiteral("failed")},
            {QLatin1String("error_code"), 42},
            {QLatin1String("error"), QStringLiteral("content policy violation")}}));
        VideoGatewayClient client3(transport3, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                   QStringLiteral("m"), 1'000, 1, [](qint64) {});
        result = client3.poll(QStringLiteral("tid"));
        QVERIFY(result.ok);
        QCOMPARE(result.state, VideoTaskState::FailedPermanent);
    }

    void pollSuccessCollectsMediaUrlsAndUsesPollPath()
    {
        FakeTransport transport;
        transport.script.append(okJson(QJsonObject{
            {QLatin1String("status"), QStringLiteral("succeeded")},
            {QLatin1String("results"), QJsonArray{QStringLiteral("http://media/x.mp4"), 7}}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("m"), 1'000, 1, [](qint64) {});

        const auto result =
            client.poll(QStringLiteral("tid"), QStringLiteral("/api/status/tid"));

        QVERIFY(result.ok);
        QCOMPARE(result.state, VideoTaskState::Succeeded);
        QCOMPARE(result.mediaUrls, QStringList{QStringLiteral("http://media/x.mp4")});
        QCOMPARE(transport.calls.first().request.url.toString(),
                 QStringLiteral("http://127.0.0.1:8765/api/status/tid"));
    }

    void unknownRawStatusStaysUnknown()
    {
        FakeTransport transport;
        transport.script.append(okJson(QJsonObject{{QLatin1String("status"),
                                                    QStringLiteral("warping")}}));
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("m"), 1'000, 1, [](qint64) {});

        const auto result = client.poll(QStringLiteral("tid"));
        QVERIFY(result.ok);
        QCOMPARE(result.state, VideoTaskState::Unknown);
    }

    void apiKeyLeakIsRedactedInPollErrors()
    {
        FakeTransport transport;
        HttpResponse broken;
        broken.networkOk = false;
        broken.errorText = QStringLiteral("host refused key gw-key-9");
        transport.script.append(broken);
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"),
                                  QStringLiteral("gw-key-9"), QStringLiteral("m"), 1'000, 1,
                                  [](qint64) {});

        const auto result = client.poll(QStringLiteral("tid"));

        QVERIFY(!result.ok);
        QVERIFY(!result.error.message.contains(QLatin1String("gw-key-9")));
    }

    void downloadWritesRenamesAndCapsSize()
    {
        FakeTransport transport;
        transport.sinkPayload = QByteArray(4096, 'v');
        transport.script.append([] {
            HttpResponse r;
            r.networkOk = true;
            r.statusCode = 200;
            return r;
        }());
        VideoGatewayClient client(transport, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                  QStringLiteral("m"), 1'000, 1, [](qint64) {});

        QTemporaryDir dir;
        const QString dest = dir.filePath(QStringLiteral("clip.mp4"));
        const auto okResult = client.download(QUrl(QStringLiteral("http://media/x.mp4")), dest,
                                              1 << 20);
        QVERIFY(okResult.ok);
        QCOMPARE(okResult.bytesWritten, qint64(4096));
        QVERIFY(QFile::exists(dest));
        QVERIFY(!QFile::exists(dest + QStringLiteral(".part")));

        // Oversize payload → transient (retryable), sink cleaned up.
        FakeTransport transport2;
        transport2.sinkPayload = QByteArray(4096, 'v');
        HttpResponse tooLarge;
        tooLarge.networkOk = false;
        tooLarge.errorText = QStringLiteral("response body exceeds limit");
        transport2.script.append(tooLarge);
        VideoGatewayClient client2(transport2, QStringLiteral("http://127.0.0.1:8765"), QString(),
                                   QStringLiteral("m"), 1'000, 1, [](qint64) {});
        const auto failResult =
            client2.download(QUrl(QStringLiteral("http://media/big.mp4")),
                             dir.filePath(QStringLiteral("big.mp4")), 1024);
        QVERIFY(!failResult.ok);
        QVERIFY(failResult.error.message.contains(QLatin1String("exceeds"))
                || failResult.error.message.contains(QLatin1String("refused")));
    }
};

QTEST_MAIN(TestVideoGateway)
#include "test_video_gateway.moc"
