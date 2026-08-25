#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtTest>

#include "../providers/FakeTransport.h"
#include "jobs/JobStore.h"
#include "media/Ffprobe.h"
#include "media/Subprocess.h"
#include "media/WhisperStt.h"
#include "media/YtDlp.h"
#include "providers/LlmClient.h"
#include "providers/TtsClient.h"
#include "redub/RedubPipeline.h"

using namespace TtvStudio;
using namespace TtvStudio::Redub;
using namespace TtvStudio::Providers;

namespace {

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

HttpResponse okSink()
{
    HttpResponse r;
    r.networkOk = true;
    r.statusCode = 200;
    return r;
}

// 2s video+audio source.
bool makeSource(const QString &ffmpeg, const QString &dest)
{
    return Media::Subprocess().run(
        ffmpeg,
        {QStringLiteral("-y"),
         QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("testsrc=duration=2:size=320x240:rate=24"),
         QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=2"),
         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
         QStringLiteral("-c:v"), QStringLiteral("libx264"),
         QStringLiteral("-c:a"), QStringLiteral("aac"),
         QStringLiteral("-shortest"), dest},
        60'000).ok();
}

} // namespace

class TestRedubPipeline : public QObject
{
    Q_OBJECT

private slots:
    void redubsALocalFileEndToEnd()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobeBin = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobeBin.isEmpty())
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        QTemporaryDir dir;

        // Local source clip handed to the pipeline directly.
        const QString localSource = dir.filePath(QStringLiteral("src.mp4"));
        QVERIFY(makeSource(ffmpeg, localSource));

        // Stub yt-dlp: not used for source_path jobs; binary must exist anyway.
        // Stub whisper.cpp: writes canned -oj JSON next to the input wav.
        const QString whisperStub = dir.filePath(QStringLiteral("whisper-stub"));
        {
            QFile f(whisperStub);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(
                "#!/bin/sh\n"
                "out=\"\"\nprev=\"\"\n"
                "for a in \"$@\"; do [ \"$prev\" = \"-of\" ] && out=\"$a\"; prev=\"$a\"; done\n"
                "cat > \"$out.json\" <<'JSON'\n"
                "{\"transcription\":[\n"
                " {\"timestamps\":{\"from\":\"00:00:00,000\",\"to\":\"00:00:01,000\"},\"text\":\" 你好\"},\n"
                " {\"timestamps\":{\"from\":\"00:00:01,000\",\"to\":\"00:00:02,000\"},\"text\":\" 再见\"}\n"
                "]}\nJSON\n"
                "exit 0\n");
            f.close();
            QVERIFY(QFile::setPermissions(whisperStub, QFile::ExeUser | QFile::ReadUser));
        }

        Jobs::JobStore store;
        QVERIFY(store.setRoot(dir.filePath(QStringLiteral("storage"))));

        FakeTransport transport;
        // Call map: 0=LLM translate, 1..2=TTS per segment (WAV sinks).
        transport.sinkPayloadOverrides.insert(
            1, makeWav(8'000)); // ~1s narration for segment 1
        transport.sinkPayloadOverrides.insert(
            2, makeWav(6'000)); // shorter narration for segment 2
        transport.script.append(okSink());
        transport.script.append(okSink());
        {
            QJsonArray translations;
            translations.append(QJsonObject{{QLatin1String("index"), 1},
                                            {QLatin1String("text"),
                                             QStringLiteral("Xin chào")}});
            translations.append(QJsonObject{{QLatin1String("index"), 2},
                                            {QLatin1String("text"),
                                             QStringLiteral("Tạm biệt")}});
            QJsonObject contentPayload{{QLatin1String("translations"), translations}};
            QJsonObject choice{{QLatin1String("message"),
                                QJsonObject{{QLatin1String("content"),
                                             QString::fromUtf8(QJsonDocument(
                                                 contentPayload).toJson())}}}};
            HttpResponse llmResponse;
            llmResponse.networkOk = true;
            llmResponse.statusCode = 200;
            llmResponse.body = QJsonDocument(
                                   QJsonObject{{QLatin1String("choices"),
                                                QJsonArray{choice}}})
                                   .toJson();
            transport.script.prepend(llmResponse);
        }

        Media::Ffprobe probe{ffprobeBin};
        Media::YtDlp ytdlp{QStringLiteral("/nonexistent/yt-dlp")}; // unused for local file
        qputenv("TTV_STUDIO_WHISPER_BIN", whisperStub.toUtf8());
        qputenv("TTV_STUDIO_WHISPER_MODEL", dir.filePath(QStringLiteral("model.bin")).toUtf8());
        { QFile m(dir.filePath(QStringLiteral("model.bin")));
          QVERIFY(m.open(QIODevice::WriteOnly)); m.write("ggml"); }

        Providers::TtsClient tts{transport, probe,
                                 QStringLiteral("http://tts.local"), 5'000, 1,
                                 [](qint64) {}};
        Providers::LlmConfig llmCfg;
        llmCfg.baseUrl = QStringLiteral("http://llm.local/v1");
        llmCfg.model = QStringLiteral("translator-x");
        llmCfg.maxAttempts = 1;
        Providers::LlmClient llm{transport, llmCfg, [](qint64) {}};
        Media::WhisperStt whisper;
        RedubPipeline pipeline{store, ytdlp, whisper, llm, tts, probe, ffmpeg,
                               RedubPipelineConfig{},
                               [](qint64 ms) { QTest::qSleep(int(ms)); }};

        const QString jobId =
            store.createJob(Jobs::Kind::Redub,
                            QJsonObject{{QLatin1String("source_path"), localSource},
                                        {QLatin1String("language"),
                                         QStringLiteral("vi")}})
                .record->id;

        QString error;
        const auto outcome = pipeline.runJob(jobId, &error);
        QCOMPARE(outcome, RunOutcome::Completed);
        QVERIFY(error.isEmpty());

        const auto record = store.loadJob(jobId);
        QVERIFY(record && record->state == Jobs::State::Completed);

        const QString root = store.jobDir(jobId);
        QVERIFY(QFile::exists(root + QStringLiteral("/input/transcript.json")));
        QVERIFY(QFile::exists(root + QStringLiteral("/input/translation.json")));
        QVERIFY(QFile::exists(root + QStringLiteral("/work/dub_track.wav")));
        QVERIFY(QFile::exists(root + QStringLiteral("/output/final_video.mp4")));

        // Final mux carries the dub track.
        const auto finalInfo = probe.probe(root + QStringLiteral("/output/final_video.mp4"));
        QVERIFY(finalInfo && finalInfo->hasVideo && finalInfo->hasAudio);
        QVERIFY(finalInfo->durationSec > 1.5);

        // Transcript artifact is well-formed and ordered.
        QFile tf(root + QStringLiteral("/input/transcript.json"));
        QVERIFY(tf.open(QIODevice::ReadOnly));
        Transcript t;
        QVERIFY(Transcript::fromJson(
            QJsonDocument::fromJson(tf.readAll()).object(), &t));
        QCOMPARE(t.segments.size(), 2);
        QCOMPARE(t.segments.first().text, QStringLiteral("你好"));
    }
};

QTEST_GUILESS_MAIN(TestRedubPipeline)
#include "test_redub_pipeline.moc"
