#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <memory>
#include <QtTest>

#include "../providers/FakeTransport.h"
#include "jobs/JobStore.h"
#include "media/Ffprobe.h"
#include "media/Subprocess.h"
#include "providers/LlmClient.h"
#include "providers/TtsClient.h"
#include "render/RenderPipeline.h"

using namespace TtvStudio;
using namespace TtvStudio::Render;
using namespace TtvStudio::Providers;

namespace {

Providers::LlmConfig llmConfig()
{
    Providers::LlmConfig config;
    config.baseUrl = QStringLiteral("http://llm.local/v1");
    config.model = QStringLiteral("planner-x");
    config.timeoutMs = 2'000;
    config.maxAttempts = 1;
    return config;
}

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

// 1s 320x240 H.264 clip served as the gateway "download".
QByteArray makeClipPayload(const QString &ffmpeg)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gen.mp4"));
    Media::Subprocess().run(
        ffmpeg,
        {QStringLiteral("-y"),
         QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("testsrc=duration=1:size=320x240:rate=24"),
         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
         QStringLiteral("-c:v"), QStringLiteral("libx264"),
         path},
        60'000);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

HttpResponse okSink()
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    return response;
}

HttpResponse jsonOk(const QJsonObject &payload)
{
    HttpResponse response;
    response.networkOk = true;
    response.statusCode = 200;
    response.body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    return response;
}

// /chat/completions reply whose content is a scenes JSON for the planner.
HttpResponse llmScenesResponse(const QString &narration, const QString &visualPrompt)
{
    QJsonObject contentPayload{
        {QLatin1String("scenes"),
         QJsonArray{QJsonObject{{QLatin1String("narration"), narration},
                                {QLatin1String("visual_prompt"), visualPrompt}}}}};
    QJsonObject choice{{QLatin1String("message"),
                        QJsonObject{{QLatin1String("content"),
                                     QString::fromUtf8(
                                         QJsonDocument(contentPayload).toJson())}}}};
    return jsonOk(QJsonObject{{QLatin1String("choices"), QJsonArray{choice}}});
}

Providers::TtsClient makeTts(FakeTransport &transport, const Media::Ffprobe &probe)
{
    return Providers::TtsClient(transport, probe,
                                QStringLiteral("http://tts.local"),
                                /*timeoutMs*/ 5'000, /*maxAttempts*/ 1, [](qint64) {});
}

Providers::LlmClient makeLlm(FakeTransport &transport)
{
    return Providers::LlmClient(transport, llmConfig(), [](qint64) {});
}

Providers::VideoGatewayClient makeVideo(FakeTransport &transport)
{
    return Providers::VideoGatewayClient(transport, QStringLiteral("http://gw.local"),
                                         QString(), QStringLiteral("veo-test"),
                                         /*requestTimeoutMs*/ 5'000, /*maxAttempts*/ 1,
                                         [](qint64) {});
}

RenderPipelineConfig fastConfig()
{
    RenderPipelineConfig config;
    config.videoPollMinMs = 5;
    config.videoPollMaxMs = 10;
    config.videoTaskBudgetMs = 60'000;
    return config;
}

struct Harness
{
    QTemporaryDir dir;
    FakeTransport transport;
    std::unique_ptr<Media::Ffprobe> probe;
    std::unique_ptr<Jobs::JobStore> store;
    std::unique_ptr<Media::Ffprobe> ffprobeForTts;

    explicit Harness(const QString &ffmpegBin, const QString &ffprobeBin)
        : probe(std::make_unique<Media::Ffprobe>(ffprobeBin))
    {
        store = std::make_unique<Jobs::JobStore>();
        QVERIFY2(store->setRoot(dir.filePath(QStringLiteral("storage"))),
                 "cannot create job storage");
        ffprobeForTts = std::make_unique<Media::Ffprobe>(ffprobeBin);
        Q_UNUSED(ffmpegBin);
    }
};

} // namespace

class TestRenderPipeline : public QObject
{
    Q_OBJECT

private slots:
    void happyPathCompletesEndToEnd()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobe.isEmpty())
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        Harness h(ffmpeg, ffprobe);

        Jobs::StoreResult job =
            h.store->createJob(Jobs::Kind::Render,
                               QJsonObject{{QLatin1String("script_text"),
                                            QStringLiteral("Một hai ba.")},
                                           {QLatin1String("language"),
                                            QStringLiteral("vi")}});
        QVERIFY(job.ok());
        const QString jobId = job.record->id;

        // TTS → LLM plan → video submit/poll/download.
        // Call map: 0=TTS(sink WAV) 1=LLM 2=submit 3=poll 4=download(sink MP4).
        h.transport.sinkPayloadOverrides.insert(0, makeWav(8'000)); // 1s narration
        h.transport.script.append(okSink());
        h.transport.script.append(llmScenesResponse(QStringLiteral("Một hai ba."),
                                                    QStringLiteral("a calm street")));
        h.transport.script.append(jsonOk(QJsonObject{{QLatin1String("task_id"),
                                                      QStringLiteral("tid-1")}}));
        h.transport.script.append(jsonOk(
            QJsonObject{{QLatin1String("status"), QStringLiteral("succeeded")},
                        {QLatin1String("results"),
                         QJsonArray{QStringLiteral("http://media.local/clip.mp4")}}}));
        h.transport.sinkPayloadOverrides.insert(4, makeClipPayload(ffmpeg));
        h.transport.script.append(okSink());

        Media::Ffprobe probe{ffprobe};
        auto tts = makeTts(h.transport, probe);
        auto llm = makeLlm(h.transport);
        auto video = makeVideo(h.transport);
        RenderPipeline pipeline{*h.store, tts, llm, video, probe, ffmpeg, fastConfig(),
                                [](qint64 ms) { QTest::qSleep(int(ms)); }};

        QString error;
        QList<std::tuple<QString, QString>> stages;
        QObject::connect(&pipeline, &RenderPipeline::stageChanged,
                         [&](const QString &, const QString &state, const QString &) {
                             stages.append({jobId, state});
                         });

        const RunOutcome outcome = pipeline.runJob(jobId, &error);

        qWarning() << "DEBUG outcome:" << int(outcome) << "err:" << error;
        QCOMPARE(outcome, RunOutcome::Completed);
        QVERIFY(error.isEmpty());

        const auto record = h.store->loadJob(jobId);
        QVERIFY(record && record->state == Jobs::State::Completed);

        // Artifacts landed where the contract says they live.
        const QString root = h.store->jobDir(jobId);
        QVERIFY(QFile::exists(root + QStringLiteral("/audio/master.wav")));
        QVERIFY(QFile::exists(root + QStringLiteral("/timeline/scenes.json")));
        QVERIFY(QFile::exists(root + QStringLiteral("/timeline/captions.vtt")));
        QVERIFY(QFile::exists(root + QStringLiteral("/clips/raw/001.mp4")));
        QVERIFY(QFile::exists(root + QStringLiteral("/output/final_video.mp4")));

        // The final mux carries both streams and is plausible in length.
        const auto finalInfo = probe.probe(root + QStringLiteral("/output/final_video.mp4"));
        QVERIFY(finalInfo && finalInfo->hasVideo && finalInfo->hasAudio);
        QVERIFY(finalInfo->durationSec > 0.4);

        // Stage walk hit every mainline state at least once (dedup consecutive).
        QStringList seenStates;
        for (const auto &entry : stages) {
            const QString state = std::get<1>(entry);
            if (seenStates.isEmpty() || seenStates.last() != state)
                seenStates.append(state);
        }
        for (const char *expected :
             {"VALIDATING", "TTS_RUNNING", "TTS_READY", "PLANNING", "SCENES_READY",
              "VIDEO_RUNNING", "CLIPS_READY", "POST_PROCESSING", "VERIFYING",
              "COMPLETED"}) {
            QVERIFY2(seenStates.contains(QLatin1String(expected)), expected);
        }
    }

    void providerBudgetExhaustionParksJobInRecoveryThenResumes()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobe.isEmpty())
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        Harness h(ffmpeg, ffprobe);

        Jobs::StoreResult job =
            h.store->createJob(Jobs::Kind::Render,
                               QJsonObject{{QLatin1String("script_text"),
                                            QStringLiteral("Một hai ba.")}});
        QVERIFY(job.ok());
        const QString jobId = job.record->id;

        Media::Ffprobe probe{ffprobe};

        // Run #1: task never leaves "running"; budget expires → recovery state.
        {
            h.transport.sinkPayloadOverrides.insert(0, makeWav(8'000));
            h.transport.script.append(okSink());                       // TTS
            h.transport.script.append(llmScenesResponse(               // LLM
                QStringLiteral("Một hai ba."), QStringLiteral("a calm street")));
            h.transport.script.append(jsonOk(QJsonObject{{QLatin1String("task_id"),
                                                          QStringLiteral("tid-slow")}}));
            // No further scripted entries — polls fail generically (transient)
            // until the tiny budget expires.

            auto tts = makeTts(h.transport, probe);
            auto llm = makeLlm(h.transport);
            auto video = makeVideo(h.transport);
            RenderPipelineConfig config = fastConfig();
            config.videoTaskBudgetMs = 80; // tiny: expire quickly
            RenderPipeline pipeline{*h.store, tts, llm, video, probe, ffmpeg, config,
                                    [](qint64 ms) { QTest::qSleep(int(ms)); }};

            QString error;
            const RunOutcome outcome = pipeline.runJob(jobId, &error);
            QCOMPARE(outcome, RunOutcome::WaitingForProvider);

            const auto record = h.store->loadJob(jobId);
            QVERIFY(record);
            QCOMPARE(record->state, Jobs::State::WaitingForProvider);
            QVERIFY(record->pendingState == Jobs::State::VideoRunning);

            // The submitted task id survived to disk ("never pay twice").
            QFile manifest(h.store->jobDir(jobId) + QStringLiteral("/timeline/scenes.json"));
            QVERIFY(manifest.open(QIODevice::ReadOnly));
            ScenePlan plan;
            QVERIFY(ScenePlan::fromJson(
                QJsonDocument::fromJson(manifest.readAll()).object(), &plan));
            QCOMPARE(plan.scenes.size(), 1);
            for (const auto &c : h.transport.calls)
                qWarning() << "DEBUG call:" << c.request.method << c.request.url.toString()
                           << "body:" << c.request.body.left(80);
            qWarning() << "DEBUG remaining script:" << h.transport.script.size();
            QCOMPARE(plan.scenes.first().providerTaskId, QStringLiteral("tid-slow"));
        }

        // Run #2: reconcile the SAME task id; now it succeeds.
        {
            h.transport.calls.clear();          // reset per-call indexing too
            h.transport.script.clear();
            h.transport.sinkPayloadOverrides.clear();
            h.transport.sinkPayloadOverrides.insert(1, makeClipPayload(ffmpeg));
            h.transport.script.append(jsonOk(
                QJsonObject{{QLatin1String("status"), QStringLiteral("succeeded")},
                            {QLatin1String("results"),
                             QJsonArray{QStringLiteral("http://media.local/clip.mp4")}}}));
            h.transport.script.append(okSink()); // download

            auto tts = makeTts(h.transport, probe);
            auto llm = makeLlm(h.transport);
            auto video = makeVideo(h.transport);
            RenderPipeline pipeline{*h.store, tts, llm, video, probe, ffmpeg, fastConfig(),
                                    [](qint64 ms) { QTest::qSleep(int(ms)); }};

            QString error;
            const RunOutcome outcome = pipeline.runJob(jobId, &error);
            QCOMPARE(outcome, RunOutcome::Completed);
            QVERIFY(error.isEmpty());

            const auto record = h.store->loadJob(jobId);
            QVERIFY(record && record->state == Jobs::State::Completed);
            // Reconciliation polled the persisted id — no new submission happened.
            QCOMPARE(h.transport.calls.first().request.url.path(),
                     QStringLiteral("/api/status/tid-slow"));
        }
    }
};

QTEST_MAIN(TestRenderPipeline)
#include "test_render_pipeline.moc"
