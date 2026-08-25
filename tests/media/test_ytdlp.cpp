#include <QJsonDocument>
#include <QStandardPaths>
#include <QtTest>

#include "media/YtDlp.h"

using namespace TtvStudio::Media;

namespace {

// Shell script posing as yt-dlp: dumps canned JSON for probe, exits 1 with
// stderr for a "bad" URL, sleeps for timeout tests.
bool writeStubScript(const QString &path)
{
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return false; // POSIX-only stubs in these tests
#else
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(
        "#!/bin/sh\n"
        "case \"$*\" in\n"
        "  *badurl*)\n"
        "    echo \"ERROR: unsupported url\" >&2\n"
        "    exit 1 ;;\n"
        "esac\n"
        "case \"$*\" in\n"
        "  *--dump-single-json*)\n"
        "    echo '{\"title\":\"Test Clip\",\"extractor_key\":\"TikTok\",\"duration\":42.5}'\n"
        "    exit 0 ;;\n"
        "esac\n"
        "exit 0\n");
    f.close();
    return QFile::setPermissions(path,
                                 QFile::ExeUser | QFile::ReadUser | QFile::WriteUser);
#endif
}

} // namespace

class TestYtDlp : public QObject
{
    Q_OBJECT

private slots:
    void missingBinaryFailsClosed()
    {
        YtDlp none{QStringLiteral("/nonexistent/yt-dlp")};
        SourceInfo info;
        IngestError error;
        QVERIFY(!none.probe(QUrl(QStringLiteral("https://x.local/v")), &info, &error));
        QVERIFY(!error.transient); // misconfiguration is Permanent
    }

    void stubProbeParsesMetadataAndDownloadSucceeds()
    {
#ifndef Q_OS_WIN
        QTemporaryDir dir;
        const QString stub = dir.filePath(QStringLiteral("yt-dlp"));
        QVERIFY(writeStubScript(stub));

        YtDlp ytdlp{stub};
        SourceInfo info;
        IngestError error;
        QVERIFY(ytdlp.probe(QUrl(QStringLiteral("https://x.local/v/1")), &info, &error));
        QCOMPARE(info.durationSec, 42.5);
        QCOMPARE(info.extractor, QStringLiteral("TikTok"));

        // Download path: stub exits 0 but the target file must exist — create
        // it the way yt-dlp would (source.mp4 next to the template).
        const QString dest = dir.filePath(QStringLiteral("job/input/source.mp4"));
        QDir().mkpath(QFileInfo(dest).absolutePath());
        { QFile out(dest); QVERIFY(out.open(QIODevice::WriteOnly)); out.write("mp4"); }
        QVERIFY(ytdlp.download(QUrl(QStringLiteral("https://x.local/v/1")), dest, &error));
#endif
    }

    void failingUrlIsPermanentWithStderrTail()
    {
#ifndef Q_OS_WIN
        QTemporaryDir dir;
        const QString stub = dir.filePath(QStringLiteral("yt-dlp"));
        QVERIFY(writeStubScript(stub));
        YtDlp ytdlp{stub};

        IngestError error;
        QVERIFY(!ytdlp.probe(QUrl(QStringLiteral("https://x.local/badurl")), nullptr,
                             &error));
        QVERIFY(error.message.contains(QLatin1String("unsupported url")));
        QVERIFY(!error.transient);
#endif
    }

    void cookiesConfiguredButMissingFailsClosed()
    {
#ifndef Q_OS_WIN
        QTemporaryDir dir;
        const QString stub = dir.filePath(QStringLiteral("yt-dlp"));
        QVERIFY(writeStubScript(stub));
        YtDlp ytdlp{stub};

        qputenv("TTV_INGEST_COOKIES_FILE", "/nonexistent/cookies.txt");
        SourceInfo info;
        IngestError error;
        QVERIFY(!ytdlp.probe(QUrl(QStringLiteral("https://x.local/v/1")), &info, &error));
        QVERIFY(error.message.contains(QLatin1String("cookies")));
        qunsetenv("TTV_INGEST_COOKIES_FILE");
#endif
    }
};

QTEST_GUILESS_MAIN(TestYtDlp)
#include "test_ytdlp.moc"
