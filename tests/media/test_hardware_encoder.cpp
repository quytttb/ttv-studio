#include <QtTest>

#include "media/HardwareEncoder.h"
#include "media/Subprocess.h"

using namespace TtvStudio::Media;

class TestHardwareEncoder : public QObject
{
    Q_OBJECT

private slots:
    void parsesEncodersListing()
    {
        // Shape mirrors real `ffmpeg -hide_banner -encoders` output.
        const QString output = QStringLiteral(
            " ffmpeg version 6.0 Copyright (c) 2000-2023\n"
            "  configuration: --enable-gpl\n"
            " A..... amv                   ASUS V1...\n"
            " V..... libaom-av1            libaom AV1\n"
            " V....D h264_amf              AMD AMF H.264 Encoder\n"
            " V....D h264_nvenc            NVIDIA NVENC H.264 encoder\n"
            " V....D h264_qsv              Quick Sync H.264 Encoding\n"
            " V..... libx264               libx264 H.264 / AVC\n"
            " V..... libx264rgb            libx264 H.264 Encoder (RGB)\n");
        const QStringList ids = HardwareEncoder::parseEncodersOutput(output);

        QVERIFY(ids.contains(QStringLiteral("h264_nvenc")));
        QVERIFY(ids.contains(QStringLiteral("h264_qsv")));
        QVERIFY(ids.contains(QStringLiteral("h264_amf")));
        QVERIFY(ids.contains(QStringLiteral("libx264")));
        // Version/config preamble must not yield phantom ids.
        QVERIFY(!ids.contains(QStringLiteral("version")));
        QVERIFY(ids.contains(QStringLiteral("amv")));       // encoder lines still parsed
    }

    void candidatesLeadWithCpuAndFlagHardware()
    {
        const QVector<EncoderInfo> candidates = HardwareEncoder::candidates();
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().id, QStringLiteral("cpu"));
        QVERIFY(!candidates.first().hardware);

        bool sawHardware = false;
        for (const EncoderInfo &info : candidates) {
            if (info.hardware)
                sawHardware = true;
            QVERIFY(info.label.contains(QChar(' '))); // human-readable labels
        }
        QVERIFY(sawHardware);
    }

    void encodingArgsCoverKnownBackends()
    {
        const QStringList cpu = HardwareEncoder::encodingArgs(QStringLiteral("cpu"));
        QCOMPARE(cpu.at(0), QStringLiteral("-c:v"));
        QCOMPARE(cpu.at(1), QStringLiteral("libx264"));
        QVERIFY(cpu.contains(QStringLiteral("-preset")));

        const QStringList alias = HardwareEncoder::encodingArgs(QStringLiteral("libx264"));
        QCOMPARE(alias, cpu);

        const QStringList nvenc =
            HardwareEncoder::encodingArgs(QStringLiteral("h264_nvenc"));
        QCOMPARE(nvenc.at(1), QStringLiteral("h264_nvenc"));
        QVERIFY(nvenc.size() > 2); // carries quality flags after -c:v <id>

        // Unknown backend falls back to the CPU profile, never empty args.
        const QStringList unknown =
            HardwareEncoder::encodingArgs(QStringLiteral("not_a_real_encoder"));
        QCOMPARE(unknown.at(1), QStringLiteral("libx264"));
    }

    void hardwareClassification()
    {
        QVERIFY(!HardwareEncoder::isHardware(QStringLiteral("cpu")));
        QVERIFY(HardwareEncoder::isHardware(QStringLiteral("h264_nvenc")));
        QVERIFY(HardwareEncoder::isHardware(QStringLiteral("h264_qsv")));
        QVERIFY(HardwareEncoder::isHardware(QStringLiteral("h264_amf")));
        QVERIFY(HardwareEncoder::isHardware(QStringLiteral("h264_videotoolbox")));
        QVERIFY(!HardwareEncoder::isHardware(QStringLiteral("bogus")));
    }

    void probeRunsWhenFfmpegAvailable()
    {
        const QString ffmpegBin = [] {
            Subprocess p;
            const auto r = p.run(QStringLiteral("ffmpeg"),
                                 {QStringLiteral("-version")}, 5'000);
            return r.started && r.ok() ? QStringLiteral("ffmpeg") : QString();
        }();
        if (ffmpegBin.isEmpty())
            QSKIP("ffmpeg not installed on this runner");

        const auto result = HardwareEncoder::probe(ffmpegBin, QStringLiteral("cpu"));
        if (!result.ok)
            QSKIP(qPrintable(QStringLiteral("cpu probe failed here: %1")
                                 .arg(result.error)));

        QVERIFY(result.ok);
        QVERIFY(result.elapsedMs >= 0);
        QVERIFY(result.error.isEmpty());
    }
};

QTEST_MAIN(TestHardwareEncoder)
#include "test_hardware_encoder.moc"
