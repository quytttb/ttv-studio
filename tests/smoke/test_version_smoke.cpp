#include <QtTest>

#include "Version_generated.h"


using namespace TtvStudio;

class TestVersionSmoke : public QObject
{
    Q_OBJECT

private slots:
    void versionConstantsAreWired()
    {
        // Expected values are injected from project(VERSION) at configure time
        // (see tests/smoke/CMakeLists.txt) — hardcoding them here would break
        // every release bump (the release workflow rewrites VERSION from the tag).
        QCOMPARE(Version::kAppMajor, kProjectVersionMajor);
        QCOMPARE(Version::kAppMinor, kProjectVersionMinor);
        QCOMPARE(Version::kAppPatch, kProjectVersionPatch);
    }

};

QTEST_MAIN(TestVersionSmoke)
#include "test_version_smoke.moc"
