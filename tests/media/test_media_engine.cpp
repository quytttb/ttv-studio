#include <QStandardPaths>
#include <QtTest>

#include "media/Ffprobe.h"
#include "media/MediaEngine.h"
#include "media/Subprocess.h"

using namespace TtvStudio::Media;

namespace {

bool haveFfmpegTools(QString *ffmpegOut, QString *ffprobeOut)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty())
        return false;
    *ffmpegOut = ffmpeg;
    *ffprobeOut = ffprobe;
    return true;
}

// 2s 320x240 testsrc clip, H.264.
bool makeClip(const QString &ffmpeg, const QString &dest, int seconds)
{
    return Subprocess()
        .run(ffmpeg,
             {QStringLiteral("-y"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"),
              QStringLiteral("testsrc=duration=%1:size=320x240:rate=24").arg(seconds),
              QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
              QStringLiteral("-c:v"), QStringLiteral("libx264"),
              dest},
             60'000)
        .ok();
}

// 2s sine-wave WAV.
bool makeWav(const QString &ffmpeg, const QString &dest)
{
    return Subprocess()
        .run(ffmpeg,
             {QStringLiteral("-y"),
              QStringLiteral("-f"), QStringLiteral("lavfi"),
              QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=2"),
              dest},
             60'000)
        .ok();
}

NormalizeTarget testTarget()
{
    NormalizeTarget target;
    target.width = 320;
    target.height = 240;
    target.fps = 24;
    return target;
}

} // namespace

class TestMediaEngine : public QObject
{
    Q_OBJECT

private slots:
    void planFitChoosesTrimRetimeFreezeOrFails()
    {
        FitPlanError error;
        auto trim = planFit(6.0, 4.0, 24, 1.10, 0.5, &error);
        QVERIFY(error.message.isEmpty());
        QCOMPARE(trim->action, FitAction::Trim);

        auto retime = planFit(3.8, 4.0, 24, 1.10, 0.5, &error);
        QCOMPARE(retime->action, FitAction::Retime);
        QCOMPARE(retime->retimeFactor, 4.0 / 3.8);

        // 3s source → stretch to 1.1 gives 3.3s; gap 0.7 > freeze budget → fail.
        auto severe = planFit(3.0, 4.0, 24, 1.10, 0.5, &error);
        QVERIFY(!severe.has_value());
        QVERIFY(!error.message.isEmpty());

        auto freeze = planFit(3.5, 4.0, 24, 1.10, 0.5, &error);
        QCOMPARE(freeze->action, FitAction::RetimeFreeze);
        QVERIFY(freeze->freezeFillSeconds > 0.0);
    }

    void fitClipTrimsAndNormalizes()
    {
        QString ffmpeg, ffprobe;
        if (!haveFfmpegTools(&ffmpeg, &ffprobe))
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        QTemporaryDir dir;
        const QString raw = dir.filePath(QStringLiteral("raw.mp4"));
        QVERIFY(makeClip(ffmpeg, raw, 2));

        Ffprobe probe{ffprobe};
        MediaEngine engine{ffmpeg, &probe};
        const auto info = probe.probe(raw);
        QVERIFY(info && info->hasVideo);

        FitPlanError planError;
        const auto decision =
            planFit(info->durationSec, 1.0, testTarget().fps, 1.10, 0.5, &planError);
        QVERIFY(planError.message.isEmpty());
        QCOMPARE(decision->action, FitAction::Trim);

        const QString fitted = dir.filePath(QStringLiteral("fitted.mp4"));
        QString error;
        QVERIFY(engine.fitClip(raw, fitted, testTarget(), *decision, &error));
        QVERIFY(error.isEmpty());

        const auto fittedInfo = probe.probe(fitted);
        QVERIFY(fittedInfo && fittedInfo->hasVideo);
        QVERIFY(qAbs(fittedInfo->durationSec - 1.0) < 0.15); // one-frame tolerance
    }

    void concatAndMuxProduceFinalTimeline()
    {
        QString ffmpeg, ffprobe;
        if (!haveFfmpegTools(&ffmpeg, &ffprobe))
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        QTemporaryDir dir;
        Ffprobe probe{ffprobe};
        MediaEngine engine{ffmpeg, &probe};

        QStringList fittedClips;
        for (int i = 0; i < 2; ++i) {
            const QString raw = dir.filePath(QStringLiteral("raw%1.mp4").arg(i));
            QVERIFY(makeClip(ffmpeg, raw, 1));
            const QString fitted = dir.filePath(QStringLiteral("fit%1.mp4").arg(i));
            QString error;
            QVERIFY(engine.fitClip(raw, fitted, testTarget(),
                                   FitDecision{FitAction::Trim, 1.0, 1.0}, &error));
            fittedClips.append(fitted);
        }

        const QString concatPath = dir.filePath(QStringLiteral("work/concat.mp4"));
        QString error;
        QVERIFY(engine.concatClips(fittedClips, concatPath, &error));
        const auto concatInfo = probe.probe(concatPath);
        QVERIFY(concatInfo && concatInfo->hasVideo);
        QVERIFY(concatInfo->durationSec > 1.9); // ≈ sum of the two clips

        const QString wav = dir.filePath(QStringLiteral("master.wav"));
        QVERIFY(makeWav(ffmpeg, wav));

        const QString finalPath = dir.filePath(QStringLiteral("output/final.mp4"));
        QVERIFY(engine.muxNarration(concatPath, wav, finalPath, 192, &error));

        const auto finalInfo = probe.probe(finalPath);
        QVERIFY(finalInfo && finalInfo->hasVideo && finalInfo->hasAudio);
        QVERIFY(finalInfo->durationSec > 1.9);
    }

    void concatRejectsEmptyListAndMuxReportsMissingInput()
    {
        QString ffmpeg, ffprobe;
        if (!haveFfmpegTools(&ffmpeg, &ffprobe))
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        Ffprobe probe{ffprobe};
        MediaEngine engine{ffmpeg, &probe};

        QTemporaryDir dir;
        QString error;
        QVERIFY(!engine.concatClips({}, dir.filePath(QStringLiteral("x.mp4")), &error));
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!engine.muxNarration(dir.filePath(QStringLiteral("missing.mp4")),
                                     dir.filePath(QStringLiteral("also-missing.wav")),
                                     dir.filePath(QStringLiteral("out.mp4")), 192, &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestMediaEngine)
#include "test_media_engine.moc"
