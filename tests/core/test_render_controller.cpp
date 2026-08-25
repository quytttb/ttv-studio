#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QtTest>

#include "../providers/FakeTransport.h"
#include "core/RenderController.h"
#include "media/Subprocess.h"

using namespace TtvStudio;
using namespace TtvStudio::Media;
using namespace TtvStudio::Core;
using namespace TtvStudio::Providers;

namespace {

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

class TestRenderController : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Endpoints are read from the environment at controller construction.
        qputenv("TTV_LLM_MODEL", "planner-x");
        qputenv("TTV_VIDEO_MODEL", "veo-test");
    }

    void rejectsEmptyScript()
    {
        QTemporaryDir dir;
        FakeTransport transport;
        RenderController controller(transport, dir.filePath(QStringLiteral("storage")));

        QCOMPARE(controller.createRenderJob(QStringLiteral("   ")), QString());
        QVERIFY(!controller.lastError().isEmpty());
        QCOMPARE(controller.jobs()->rowCount(), 0);
    }

    void runsAJobToEndThroughWorkerThread()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        if (ffmpeg.isEmpty() || ffprobe.isEmpty())
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        QTemporaryDir dir;
        FakeTransport transport;

        // Call map: 0=TTS(sink WAV) 1=LLM 2=submit 3=poll 4=download(sink MP4).
        transport.sinkPayloadOverrides.insert(0, makeWav(8'000));
        transport.script.append(okSink());
        transport.script.append(llmScenesResponse(QStringLiteral("Một hai ba."),
                                                  QStringLiteral("a calm street")));
        transport.script.append(jsonOk(QJsonObject{{QLatin1String("task_id"),
                                                    QStringLiteral("tid-ctl-1")}}));
        transport.script.append(jsonOk(
            QJsonObject{{QLatin1String("status"), QStringLiteral("succeeded")},
                        {QLatin1String("results"),
                         QJsonArray{QStringLiteral("http://media.local/clip.mp4")}}}));
        // MP4 sink payload generated with the local ffmpeg.
        {
            QTemporaryDir genDir;
            const QString genPath = genDir.filePath(QStringLiteral("gen.mp4"));
            Media::Subprocess().run(
                ffmpeg,
                {QStringLiteral("-y"),
                 QStringLiteral("-f"), QStringLiteral("lavfi"),
                 QStringLiteral("-i"),
                 QStringLiteral("testsrc=duration=1:size=320x240:rate=24"),
                 QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                 QStringLiteral("-c:v"), QStringLiteral("libx264"),
                 genPath},
                60'000);
            QFile gen(genPath);
            const QByteArray clipBytes = gen.open(QIODevice::ReadOnly)
                                             ? gen.readAll()
                                             : QByteArray();
            transport.sinkPayloadOverrides.insert(4, clipBytes);
        }
        transport.script.append(okSink()); // 4 = download

        RenderController controller(transport, dir.filePath(QStringLiteral("storage")));

        const QString jobId = controller.createRenderJob(
            QStringLiteral("Một hai ba."), QStringLiteral("vi"), QStringLiteral("16:9"),
            QStringLiteral("720p"));
        QVERIFY(!jobId.isEmpty());
        QCOMPARE(controller.jobs()->rowCount(), 1);

        QSignalSpy finishedSpy(&controller, &RenderController::jobFinished);
        QVERIFY(finishedSpy.isValid());
        connect(&controller, &RenderController::runStateChanged, &controller,
                [&controller] { qWarning() << "DEBUG stage:" << controller.activeStage(); },
                Qt::QueuedConnection);

        controller.runJob(jobId);
        QVERIFY(controller.runActive());

        // Pipeline runs on a worker thread; the GUI thread just waits.
        QVERIFY2(finishedSpy.wait(30'000), qPrintable(QStringLiteral("job did not finish; lastError=%1").arg(controller.lastError())));
        QCOMPARE(finishedSpy.first().at(0).toString(), jobId);
        QCOMPARE(finishedSpy.first().at(1).toBool(), true);
        QVERIFY(!controller.runActive());
        QVERIFY(controller.lastError().isEmpty());

        // Model reflects terminal state and the artifact exists on disk.
        const QModelIndex idx = controller.jobs()->index(0, 0);
        QCOMPARE(controller.jobs()->data(idx, JobListModel::StateRole).toString(),
                 QStringLiteral("COMPLETED"));
        QVERIFY(QFile::exists(controller.finalVideoPath(jobId)));
    }

    void doubleRunIsRejected()
    {
        QTemporaryDir dir;
        FakeTransport transport;
        RenderController controller(transport, dir.filePath(QStringLiteral("storage")));

        // A job that will stall forever (script exhausted → transient polls)
        const QString jobId = controller.createRenderJob(QStringLiteral("Một hai ba."));
        QVERIFY(!jobId.isEmpty());

        QSignalSpy finishedSpy(&controller, &RenderController::jobFinished);
        controller.runJob(jobId);
        QVERIFY(controller.runActive());

        // Second run while active must be rejected without crashing.
        controller.runJob(jobId);
        QVERIFY(!controller.lastError().isEmpty());

        controller.cancelRun();
        QVERIFY2(finishedSpy.wait(60'000), "cancelled job did not finish");
    }
};

QTEST_GUILESS_MAIN(TestRenderController)
#include "test_render_controller.moc"
