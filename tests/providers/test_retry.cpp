#include <QList>
#include <QtTest>

#include "providers/Retry.h"

using namespace TtvStudio::Providers;

namespace {

struct ProbeResult
{
    bool ok = false;
    ProviderError error;
};

} // namespace

class TestRetry : public QObject
{
    Q_OBJECT

private slots:
    void backoffStaysWithinBounds()
    {
        // attempt 1 → 1..2s, attempt 2 → 2..4s, attempt ≥3 → capped 4..8s.
        for (int i = 0; i < 25; ++i) {
            const qint64 a1 = backoffDelayMs(1);
            QVERIFY(a1 >= 500 && a1 <= 2000);
            const qint64 a2 = backoffDelayMs(2);
            QVERIFY(a2 >= 1000 && a2 <= 4000);
            const qint64 a3 = backoffDelayMs(3);
            QVERIFY(a3 >= 4000 && a3 <= 8000);
            const qint64 a9 = backoffDelayMs(9);
            QVERIFY(a9 >= 4000 && a9 <= 8000);
        }
    }

    void retriesTransientUntilSuccess()
    {
        int invocations = 0;
        QList<qint64> delays;
        const auto sleep = [&](qint64 ms) { delays.append(ms); };

        const ProbeResult result = retryTransient<ProbeResult>(3, sleep, [&]() -> ProbeResult {
            ++invocations;
            if (invocations < 3)
                return ProbeResult{false,
                                   {ErrorKind::Transient, QStringLiteral("p"),
                                    QStringLiteral("boom"), 0}};
            return ProbeResult{true, {}};
        });

        QVERIFY(result.ok);
        QCOMPARE(invocations, 3);
        QCOMPARE(delays.size(), 2); // slept between the three attempts only
    }

    void permanentFailsImmediately()
    {
        int invocations = 0;
        const auto sleep = [](qint64) { QFAIL("no sleep expected"); };

        const ProbeResult result = retryTransient<ProbeResult>(3, sleep, [&]() -> ProbeResult {
            ++invocations;
            return ProbeResult{false,
                               {ErrorKind::Permanent, QStringLiteral("p"),
                                QStringLiteral("bad request"), 400}};
        });

        QVERIFY(!result.ok);
        QCOMPARE(invocations, 1);
        QCOMPARE(result.error.kind, ErrorKind::Permanent);
    }

    void ambiguousTimeoutDoesNotRetry()
    {
        int invocations = 0;
        const auto sleep = [](qint64) {};

        const ProbeResult result = retryTransient<ProbeResult>(3, sleep, [&]() -> ProbeResult {
            ++invocations;
            return ProbeResult{false,
                               {ErrorKind::AmbiguousTimeout, QStringLiteral("p"),
                                QStringLiteral("timed out"), 0}};
        });

        QVERIFY(!result.ok);
        QCOMPARE(invocations, 1);
    }

    void exhaustedAttemptsReturnLastTransientError()
    {
        int invocations = 0;
        const auto sleep = [](qint64) {};

        const ProbeResult result = retryTransient<ProbeResult>(3, sleep, [&]() -> ProbeResult {
            ++invocations;
            return ProbeResult{false,
                               {ErrorKind::Transient, QStringLiteral("p"),
                                QStringLiteral("down"), 503}};
        });

        QVERIFY(!result.ok);
        QCOMPARE(invocations, 3);
        QCOMPARE(result.error.statusCode, 503);
    }
};

QTEST_MAIN(TestRetry)
#include "test_retry.moc"
