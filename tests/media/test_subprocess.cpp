#include <QStandardPaths>
#include <QtTest>

#include "media/Subprocess.h"

using TtvStudio::Media::Subprocess;
using TtvStudio::Media::SubprocessResult;

class TestSubprocess : public QObject
{
    Q_OBJECT

private slots:
    void capturesExitCodeAndStreams()
    {
#ifdef Q_OS_WIN
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("cmd"));
        if (shell.isEmpty())
            QSKIP("cmd.exe not available");
        // Mirror the POSIX command's observable behavior: stdout "hello",
        // stderr "oops", exit code 3.
        SubprocessResult result = Subprocess().run(
            shell,
            {QStringLiteral("/c"),
             QStringLiteral("echo hello && echo oops 1>&2 && exit 3")});
#else
        const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
        if (shell.isEmpty())
            QSKIP("sh not available");
        SubprocessResult result = Subprocess().run(
            shell, {QStringLiteral("-c"), QStringLiteral("echo hello; echo oops >&2; exit 3")});
#endif
        QVERIFY(result.started);
        QVERIFY(!result.timedOut);
        QCOMPARE(result.exitCode, 3);
        QVERIFY(result.stdoutText.contains(QLatin1String("hello")));
        QVERIFY(result.stderrText.contains(QLatin1String("oops")));
        QVERIFY(!result.ok());
    }

    void killsOnTimeout()
    {
#ifndef Q_OS_WIN
        const QString sleepBin = QStandardPaths::findExecutable(QStringLiteral("sleep"));
        if (sleepBin.isEmpty())
            QSKIP("sleep not available");
        SubprocessResult result = Subprocess().run(sleepBin,
                                                   {QStringLiteral("30")},
                                                   300);
        QVERIFY(result.started);
        QVERIFY(result.timedOut);
        QVERIFY(!result.ok());
#else
        QSKIP("timeout kill verified on POSIX runners only");
#endif
    }

    void reportsFailureToStart()
    {
        SubprocessResult result = Subprocess().run(
            QStringLiteral("definitely-not-a-real-binary-xyz"), {}, 1000);
        QVERIFY(!result.started);
        QVERIFY(!result.ok());
        QVERIFY(result.stderrText.contains(QLatin1String("failed to start")));
    }
};

QTEST_MAIN(TestSubprocess)
#include "test_subprocess.moc"
