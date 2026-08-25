#include <QtTest>

#include "render/ScriptCoverage.h"

using namespace TtvStudio::Render;

class TestScriptCoverage : public QObject
{
    Q_OBJECT

private slots:
    void normalizeCollapsesWhitespaceAndNbsp()
    {
        QCOMPARE(normalizeScript(QStringLiteral("  Xin   chào \u00A0 thế giới.\n")),
                 QStringLiteral("Xin chào thế giới."));
    }

    void keyStripsAllWhitespace()
    {
        QCOMPARE(normalizedKey(QStringLiteral("a b\tc\nd")),
                 QStringLiteral("abcd"));
    }

    void exactCoveragePasses()
    {
        const QString script = QStringLiteral("Câu thứ nhất. Câu thứ hai. Câu cuối cùng.");
        const QStringList scenes{
            QStringLiteral("Câu thứ nhất."),
            QStringLiteral("Câu thứ hai."),
            QStringLiteral("Câu cuối cùng."),
        };
        QVERIFY(verifyCoverage(normalizedKey(script), scenes).isEmpty());
    }

    void coverageIgnoresWhitespaceDifferences()
    {
        const QString script = QStringLiteral("Một hai ba bốn");
        const QStringList scenes{
            QStringLiteral("Một  hai"), // extra internal whitespace
            QStringLiteral("ba\nbốn"),  // newline inside the narration
        };
        QVERIFY(verifyCoverage(normalizedKey(script), scenes).isEmpty());
    }

    void droppedTextIsDetected()
    {
        const QString script = QStringLiteral("Một hai ba bốn năm");
        const QStringList scenes{QStringLiteral("Một hai")};
        const auto problems = verifyCoverage(normalizedKey(script), scenes);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().sceneIndex, 0); // plan-level problem
        QVERIFY(problems.first().message.contains(QLatin1String("dropped")));
    }

    void rewrittenSegmentIsRejected()
    {
        const QString script = QStringLiteral("Một hai ba bốn");
        const QStringList scenes{QStringLiteral("Một hai"), QStringLiteral("năm sáu")};
        const auto problems = verifyCoverage(normalizedKey(script), scenes);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().sceneIndex, 2);
        QVERIFY(problems.first().message.contains(QLatin1String("contiguous")));
    }

    void reorderedSegmentsAreRejected()
    {
        const QString script = QStringLiteral("Một hai ba bốn");
        const QStringList scenes{QStringLiteral("hai ba"), QStringLiteral("Một bốn")};
        const auto problems = verifyCoverage(normalizedKey(script), scenes);
        QVERIFY(!problems.isEmpty());
    }

    void emptyNarrationIsRejected()
    {
        const auto problems = verifyCoverage(QStringLiteral("abc"), {QStringLiteral("")});
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().sceneIndex, 1);
    }
};

QTEST_MAIN(TestScriptCoverage)
#include "test_script_coverage.moc"
