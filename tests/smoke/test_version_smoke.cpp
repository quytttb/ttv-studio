#include <QtTest>

#include "Version_generated.h"
#include "utils/AppConstants.h"

using namespace TtvStudio;

class TestVersionSmoke : public QObject
{
    Q_OBJECT

private slots:
    void versionConstantsAreWired()
    {
        static_assert(Version::kAppMajor >= 0, "major version must be generated");
        QCOMPARE(Version::kAppMinor, 1);
        QCOMPARE(Version::kAppPatch, 1);
    }

    void intervalBoundsAreSane()
    {
        QVERIFY(Defaults::kMinIntervalSec < Defaults::kMaxIntervalSec);
        QVERIFY(Defaults::kLogMaxBytes > 0);
        QVERIFY(Defaults::kLogKeepBackups >= 1);
    }
};

QTEST_MAIN(TestVersionSmoke)
#include "test_version_smoke.moc"
