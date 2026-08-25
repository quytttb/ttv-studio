#include <QFile>
#include <QStandardPaths>
#include <QtMath>
#include <QtTest>

#include "FakeTransport.h"
#include "media/Ffprobe.h"
#include "providers/TtsClient.h"

using namespace TtvStudio::Providers;

namespace {

constexpr int kMaxAttempts = 3;

HttpResponse okWithSink(qint64 reportedBytes = 0)
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.bytesReceived = reportedBytes; // 0 → FakeTransport fills from payload
    return response;
}

HttpResponse httpStatus(int statusCode)
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = statusCode;
    return response;
}

// Minimal mono 16-bit 8 kHz PCM WAV (~1s) that ffprobe parses without ffmpeg.
QByteArray makeWav(int sampleCount)
{
    const int dataBytes = sampleCount * 2;
    QByteArray wav;
    wav.append(QByteArrayLiteral("RIFF"));
    const quint32 chunkSize = quint32(36 + dataBytes);
    wav.append(reinterpret_cast<const char *>(&chunkSize), 4);
    wav.append(QByteArrayLiteral("WAVEfmt "));
    const quint32 fmtSize = 16;
    wav.append(reinterpret_cast<const char *>(&fmtSize), 4);
    const quint16 pcm = 1, channels = 1, bits = 16;
    const quint32 rate = 8000, byteRate = 16000, blockAlign = 2;
    wav.append(reinterpret_cast<const char *>(&pcm), 2);
    wav.append(reinterpret_cast<const char *>(&channels), 2);
    wav.append(reinterpret_cast<const char *>(&rate), 4);
    wav.append(reinterpret_cast<const char *>(&byteRate), 4);
    wav.append(reinterpret_cast<const char *>(&blockAlign), 2);
    wav.append(reinterpret_cast<const char *>(&bits), 2);
    wav.append(QByteArrayLiteral("data"));
    wav.append(reinterpret_cast<const char *>(&dataBytes), 4);
    for (int i = 0; i < sampleCount; ++i) {
        const qint16 s = qint16(8000 * qSin(i * 0.05));
        wav.append(reinterpret_cast<const char *>(&s), 2);
    }
    return wav;
}

} // namespace

class TestTtsClient : public QObject
{
    Q_OBJECT

private slots:
    void emptyTextFailsClosedWithoutCalling()
    {
        FakeTransport transport;
        TtsClient client(transport, TtvStudio::Media::Ffprobe{}, QStringLiteral("http://127.0.0.1:3900"),
                         1'000, kMaxAttempts, [](qint64) {});

        const auto result = client.synthesize({}, QStringLiteral("/tmp/whatever.wav"));

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QCOMPARE(transport.calls.size(), 0);
    }

    void requestIsMultipartToGenerateEndpoint()
    {
        FakeTransport transport;
        // Script a permanent failure so we exercise only the request shape.
        transport.script.append(httpStatus(400));
        TtsClient client(transport, TtvStudio::Media::Ffprobe{}, QStringLiteral("http://127.0.0.1:3900"),
                         1'000, kMaxAttempts, [](qint64) {});

        (void)client.synthesize(TtsRequest{QStringLiteral("xin chào")},
                                QStringLiteral("/tmp/out.wav"));

        QCOMPARE(transport.calls.size(), 1);
        const auto &call = transport.calls.first();
        QCOMPARE(call.request.url.toString(), QStringLiteral("http://127.0.0.1:3900/generate"));
        const QString contentType =
            call.request.headerValue(QStringLiteral("Content-Type"));
        QVERIFY(contentType.startsWith(QLatin1String("multipart/form-data; boundary=")));
        const QString body = QString::fromUtf8(call.request.body);
        QVERIFY(body.contains(QLatin1String("name=\"text\"")));
        QVERIFY(body.contains(QStringLiteral("xin chào")));
        QVERIFY(body.contains(QLatin1String("name=\"language\"")));
        QVERIFY(body.contains(QLatin1String("vi")));
        QVERIFY(body.contains(QLatin1String("speed"))); // default speed serialized
    }

    void implausiblySmallBodyIsPermanentAndPartRemoved()
    {
        FakeTransport transport;
        transport.sinkPayload = QByteArray(100, 'x'); // < 512 bytes minimum
        transport.script.append(okWithSink());
        TtsClient client(transport, TtvStudio::Media::Ffprobe{}, QStringLiteral("http://127.0.0.1:3900"),
                         1'000, kMaxAttempts, [](qint64) {});

        QTemporaryDir dir;
        const QString dest = dir.filePath(QStringLiteral("narration.wav"));
        const auto result = client.synthesize(TtsRequest{QStringLiteral("text")}, dest);

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QVERIFY(result.error.message.contains(QLatin1String("implausibly small")));
        QVERIFY(!QFile::exists(dest + QStringLiteral(".part")));
        QVERIFY(!QFile::exists(dest));
    }

    void transientHttpRetriesBeforeFailing()
    {
        FakeTransport transport;
        transport.script.append(httpStatus(503));
        transport.script.append(httpStatus(503));
        transport.script.append(httpStatus(503));
        int sleeps = 0;
        TtsClient client(transport, TtvStudio::Media::Ffprobe{}, QStringLiteral("http://127.0.0.1:3900"),
                         1'000, kMaxAttempts, [&](qint64) { ++sleeps; });

        QTemporaryDir dir;
        const auto result = client.synthesize(TtsRequest{QStringLiteral("text")},
                                              dir.filePath(QStringLiteral("a.wav")));

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Transient);
        QCOMPARE(result.error.statusCode, 503);
        QCOMPARE(transport.calls.size(), kMaxAttempts);
        QCOMPARE(sleeps, kMaxAttempts - 1);
    }

    void successStreamsRenamesAndProbesAudio()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("ffprobe not installed on this runner");

        FakeTransport transport;
        transport.sinkPayload = makeWav(8'000); // ~1s of audio
        transport.script.append(okWithSink());

        TtvStudio::Media::Ffprobe ffprobe{
            QStandardPaths::findExecutable(QStringLiteral("ffprobe"))};
        TtsClient client(transport, ffprobe, QStringLiteral("http://127.0.0.1:3900"), 5'000,
                         kMaxAttempts, [](qint64) {});

        QTemporaryDir dir;
        const QString dest = dir.filePath(QStringLiteral("narration.wav"));
        const auto result = client.synthesize(TtsRequest{QStringLiteral("kể chuyện")}, dest);

        QVERIFY(result.ok);
        QCOMPARE(result.audioPath, dest);
        QVERIFY(result.durationSec > 0.0);
        QVERIFY(QFile::exists(dest));                 // renamed into place
        QVERIFY(!QFile::exists(dest + QStringLiteral(".part"))); // no leftover part
    }

    void unprobeableAudioFailsClosed()
    {
        FakeTransport transport;
        transport.sinkPayload = QByteArray(2048, '\x07'); // big but not media
        transport.script.append(okWithSink());

        // Point at a guaranteed-missing binary so probe fails deterministically.
        TtvStudio::Media::Ffprobe missing{QStringLiteral("/nonexistent/ffprobe")};
        TtsClient client(transport, missing, QStringLiteral("http://127.0.0.1:3900"), 1'000,
                         kMaxAttempts, [](qint64) {});

        QTemporaryDir dir;
        const QString dest = dir.filePath(QStringLiteral("bad.wav"));
        const auto result = client.synthesize(TtsRequest{QStringLiteral("text")}, dest);

        QVERIFY(!result.ok);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
        QVERIFY(!QFile::exists(dest + QStringLiteral(".part")));
        QVERIFY(!QFile::exists(dest));
    }
};

QTEST_MAIN(TestTtsClient)
#include "test_tts_client.moc"
