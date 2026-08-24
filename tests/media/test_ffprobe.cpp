#include <QStandardPaths>
#include <QtTest>

#include "media/Ffprobe.h"
#include "media/Subprocess.h"

using TtvStudio::Media::Ffprobe;
using TtvStudio::Media::MediaInfo;

class TestFfprobe : public QObject
{
    Q_OBJECT

private slots:
    void probesMissingBinary()
    {
        Ffprobe probe{QStringLiteral("/nonexistent/ffprobe")};
        const auto info = probe.probe(QStringLiteral("/tmp/anything.mp4"));
        QVERIFY(!info.has_value());
    }

    void rejectsGarbageFile()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("ffprobe not installed on this runner");

        QTemporaryDir dir;
        const QString garbage = dir.filePath(QStringLiteral("garbage.mp4"));
        QFile f(garbage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not an media file at all");
        f.close();

        const auto info = Ffprobe{}.probe(garbage);
        QVERIFY(!info.has_value()); // fail closed on undecodable input
    }

    void parsesRealMediaWhenAvailable()
    {
        // Generates a tiny sine-wave MP4 with ffmpeg when both binaries are
        // present, then verifies the probe round-trip.
        const QString ffprobe =
            QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
        const QString ffmpeg =
            QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffprobe.isEmpty() || ffmpeg.isEmpty())
            QSKIP("ffmpeg/ffprobe not installed on this runner");

        QTemporaryDir dir;
        const QString media = dir.filePath(QStringLiteral("clip.mp4"));
        const TtvStudio::Media::SubprocessResult gen = TtvStudio::Media::Subprocess().run(
            ffmpeg,
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-f"), QStringLiteral("lavfi"),
             QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=1"),
             QStringLiteral("-f"), QStringLiteral("lavfi"),
             QStringLiteral("-i"), QStringLiteral("testsrc=duration=1:size=64x64:rate=10"),
             QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
             QStringLiteral("-y"), media},
            60'000);
        if (!gen.ok())
            QSKIP("lavfi sources unavailable in this ffmpeg build");

        const auto info = Ffprobe{}.probe(media);
        QVERIFY(info.has_value());
        QCOMPARE(info->hasVideo, true);
        QCOMPARE(info->hasAudio, true);
        QVERIFY(info->durationSec > 0.5 && info->durationSec < 3.0);
        QCOMPARE(info->width, 64);
        QCOMPARE(info->height, 64);
    }
};

QTEST_MAIN(TestFfprobe)
#include "test_ffprobe.moc"
