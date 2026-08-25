#include <QJsonDocument>
#include <QtTest>

#include "redub/Transcript.h"

using namespace TtvStudio::Redub;

namespace {

Transcript sampleTranscript()
{
    Transcript t;
    t.language = QStringLiteral("zh");
    t.provider = QStringLiteral("whisper_local");
    t.model = QStringLiteral("small");
    t.segments = {
        {1, 0.0, 2.5, QStringLiteral("大家好")},
        {2, 2.5, 6.0, QStringLiteral("今天我们聊聊人工智能。")},
        {3, 6.0, 9.25, QStringLiteral("这是一个很长的句子需要更多时间。")},
    };
    return t;
}

} // namespace

class TestTranscript : public QObject
{
    Q_OBJECT

private slots:
    void transcriptJsonRoundTrip()
    {
        const Transcript original = sampleTranscript();
        Transcript restored;
        QVERIFY(Transcript::fromJson(original.toJson(), &restored));

        QCOMPARE(restored.language, original.language);
        QCOMPARE(restored.segments.size(), original.segments.size());
        for (int i = 0; i < original.segments.size(); ++i) {
            QCOMPARE(restored.segments.at(i).index, original.segments.at(i).index);
            QCOMPARE(restored.segments.at(i).startSeconds,
                     original.segments.at(i).startSeconds);
            QCOMPARE(restored.segments.at(i).endSeconds,
                     original.segments.at(i).endSeconds);
            QCOMPARE(restored.segments.at(i).text, original.segments.at(i).text);
        }
    }

    void malformedTranscriptFailsClosed()
    {
        Transcript out;
        // Empty segments.
        QVERIFY(!Transcript::fromJson(QJsonObject{{QLatin1String("segments"),
                                                   QJsonArray{}}},
                                      &out));
        // Non-contiguous indices (skips #2).
        const QJsonArray gap{QJsonObject{{QLatin1String("index"), 1},
                                         {QLatin1String("start_seconds"), 0.0},
                                         {QLatin1String("end_seconds"), 1.0},
                                         {QLatin1String("text"), QStringLiteral("a")}},
                             QJsonObject{{QLatin1String("index"), 3},
                                         {QLatin1String("start_seconds"), 1.0},
                                         {QLatin1String("end_seconds"), 2.0},
                                         {QLatin1String("text"), QStringLiteral("b")}}};
        QVERIFY(!Transcript::fromJson(QJsonObject{{QLatin1String("segments"), gap}}, &out));
        // Negative duration window.
        const QJsonArray inverted{
            QJsonObject{{QLatin1String("index"), 1},
                        {QLatin1String("start_seconds"), 5.0},
                        {QLatin1String("end_seconds"), 1.0},
                        {QLatin1String("text"), QStringLiteral("a")}}};
        QVERIFY(!Transcript::fromJson(QJsonObject{{QLatin1String("segments"), inverted}},
                                      &out));
        // Missing text.
        const QJsonArray noText{QJsonObject{{QLatin1String("index"), 1},
                                            {QLatin1String("start_seconds"), 0.0},
                                            {QLatin1String("end_seconds"), 1.0}}};
        QVERIFY(!Transcript::fromJson(QJsonObject{{QLatin1String("segments"), noText}},
                                      &out));
    }

    void translationJsonRoundTrip()
    {
        Translation original;
        original.targetLanguage = QStringLiteral("vi");
        original.sourceLanguage = QStringLiteral("zh");
        original.segments = {{1, QStringLiteral("Xin chào mọi người")},
                             {2, QStringLiteral("Hôm nay chúng ta nói về AI.")}};
        Translation restored;
        QVERIFY(Translation::fromJson(original.toJson(), &restored));
        QCOMPARE(restored.targetLanguage, QStringLiteral("vi"));
        QCOMPARE(restored.segments.size(), 2);
        QCOMPARE(restored.segments.last().text, QStringLiteral("Hôm nay chúng ta nói về AI."));

        // Empty target language fails closed.
        Translation bad{{}, {}, {{1, QStringLiteral("x")}}};
        bad.targetLanguage.clear();
        QVERIFY(!Translation::fromJson(bad.toJson(), &restored));
    }
};

QTEST_GUILESS_MAIN(TestTranscript)
#include "test_transcript.moc"
