#include <QtTest>

#include "render/Captions.h"

using namespace TtvStudio::Render;

class TestCaptions : public QObject
{
    Q_OBJECT

private slots:
    void splitsSentencesAndWrapsLongOnes()
    {
        const QStringList sentences =
            splitSentences(QStringLiteral("Câu một. Câu hai! Câu ba?"));
        QCOMPARE(sentences.size(), 3);

        // A single very long "sentence" wraps on word boundaries.
        QString longSentence;
        for (int i = 0; i < 40; ++i)
            longSentence += QStringLiteral("từ%1 ").arg(i);
        const auto wrapped = splitSentences(longSentence);
        QVERIFY(wrapped.size() > 1);
        for (const QString &cue : wrapped)
            QVERIFY(cue.size() <= 96);
    }

    void proportionalCuesSpanTheDuration()
    {
        const auto cues = proportionalCues(QStringLiteral("Ngắn. Đây là câu dài hơn hẳn."), 10.0);
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.first().index, 1);
        QVERIFY(qAbs(cues.first().startSeconds) < 1e-9);
        // The last cue ends exactly at the total duration.
        QVERIFY(qAbs(cues.last().endSeconds - 10.0) < 1e-6);
        // Cues are contiguous.
        QVERIFY(cues.last().startSeconds >= cues.first().endSeconds - 1e-9);
        // Longer sentence gets a larger share than its short neighbor.
        QVERIFY((cues.last().endSeconds - cues.last().startSeconds)
                > (cues.first().endSeconds - cues.first().startSeconds));
    }

    void emptyInputYieldsNoCues()
    {
        QVERIFY(proportionalCues(QString(), 5.0).isEmpty());
        QVERIFY(proportionalCues(QStringLiteral("text"), -1.0).isEmpty());
    }

    void rendersWebVttDocument()
    {
        QVector<CaptionCue> cues;
        cues.append({1, 0.0, 1.5, QStringLiteral("Xin chào")});
        cues.append({2, 1.5, 3.25, QStringLiteral("Thế giới")});

        const QString vtt = renderVtt(cues);
        QVERIFY(vtt.startsWith(QLatin1String("WEBVTT\n")));
        QVERIFY(vtt.contains(QLatin1String("00:00:00.000 --> 00:00:01.500")));
        QVERIFY(vtt.contains(QLatin1String("00:00:01.500 --> 00:00:03.250")));
        QVERIFY(vtt.contains(QStringLiteral("Xin chào")));
        QVERIFY(vtt.contains(QStringLiteral("Thế giới")));
    }
};

QTEST_MAIN(TestCaptions)
#include "test_captions.moc"
