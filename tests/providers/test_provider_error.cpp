#include <QtTest>

#include "providers/ProviderError.h"

using namespace TtvStudio::Providers;

class TestProviderError : public QObject
{
    Q_OBJECT

private slots:
    void redactsAuthorizationHeader()
    {
        const QString msg = QStringLiteral("Authorization: Bearer sk-abc123def456 failed");
        QCOMPARE(redactMessage(msg), QStringLiteral("Authorization: [REDACTED] failed"));
    }

    void redactsBearerTokenStandalone()
    {
        const QString msg = QStringLiteral("connect failed for bearer Abcdef12+/=_-xy");
        QCOMPARE(redactMessage(msg), QStringLiteral("connect failed for Bearer [REDACTED]"));
    }

    void redactsUrlTokenParams()
    {
        const QString msg = QStringLiteral("GET https://x.y/a?api_key=SECRET123&b=2 boom");
        QVERIFY(!redactMessage(msg).contains(QLatin1String("SECRET123")));
        QVERIFY(redactMessage(msg).contains(QLatin1String("api_key=[REDACTED]")));
    }

    void redactsKnownSecretValue()
    {
        QCOMPARE(redactValue(QStringLiteral("key=xyz and xyz again"), QStringLiteral("xyz")),
                 QStringLiteral("key=[REDACTED] and [REDACTED] again"));
        // Empty secret is a no-op.
        QCOMPARE(redactValue(QStringLiteral("untouched"), QString()),
                 QStringLiteral("untouched"));
    }

    void classifiesTransientHttpStatuses()
    {
        for (const int code : {408, 425, 429, 500, 502, 503, 504})
            QVERIFY(isTransientHttpStatus(code));
        for (const int code : {200, 400, 401, 403, 404, 418, 422})
            QVERIFY(!isTransientHttpStatus(code));
        QVERIFY(isTransientHttpStatus(505)); // generic 5xx family
    }
};

QTEST_MAIN(TestProviderError)
#include "test_provider_error.moc"
