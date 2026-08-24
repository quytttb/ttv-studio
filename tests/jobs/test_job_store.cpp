#include <QTemporaryDir>
#include <QtTest>

#include "jobs/JobStore.h"

using namespace TtvStudio::Jobs;

class TestJobStore : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_store = std::make_unique<JobStore>();
        QVERIFY(m_store->setRoot(m_dir.path()));
    }

    void createJobBuildsArtifactSkeletonAndRecord()
    {
        const StoreResult result = m_store->createJob(
            Kind::Redub,
            QJsonObject{{QStringLiteral("source"),
                         QStringLiteral("https://v.douyin.com/xxx/")},
                        {QStringLiteral("target_language"),
                         QStringLiteral("vi")}});

        QVERIFY(result.ok());
        QCOMPARE(result.record->state, State::Created);
        QCOMPARE(result.record->kind, Kind::Redub);

        QDir jobDir(m_store->jobDir(result.record->id));
        QVERIFY(jobDir.exists(QStringLiteral("job.json")));
        QVERIFY(jobDir.exists(QStringLiteral("input")));
        QVERIFY(jobDir.exists(QStringLiteral("work")));
        QVERIFY(jobDir.exists(QStringLiteral("output")));
        QVERIFY(!QFile::exists(jobDir.filePath(QStringLiteral("job.json.part"))));
    }

    void rejectsDuplicateExplicitId()
    {
        const auto first = m_store->createJob(Kind::Render, {}, QStringLiteral("abc123"));
        QVERIFY(first.ok());

        const auto second = m_store->createJob(Kind::Render, {}, QStringLiteral("abc123"));
        QVERIFY(!second.ok());
        QVERIFY(second.error.contains(QLatin1String("already exists")));
    }

    void roundTripThroughDisk()
    {
        const auto created = m_store->createJob(Kind::Render, {});
        QVERIFY(created.ok());

        const auto loaded = m_store->loadJob(created.record->id);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->id, created.record->id);
        QCOMPARE(loaded->kind, Kind::Render);
        QCOMPARE(loaded->state, State::Created);

        // A second store instance (fresh process simulation) reads the same
        // record — the resume guarantee.
        JobStore reopened;
        QVERIFY(reopened.setRoot(m_dir.path()));
        const auto reread = reopened.loadJob(created.record->id);
        QVERIFY(reread.has_value());
        QCOMPARE(reread->state, State::Created);
    }

    void legalTransitionPersistsIllegalIsRejected()
    {
        auto record = *m_store->createJob(Kind::Redub,
                                          QJsonObject{
                                              {QStringLiteral("source"),
                                               QStringLiteral("/tmp/src.mp4")}})
                           .record;
        record.state = State::Validating;
        QVERIFY(m_store->updateJob(record).ok());

        // Skip ahead → rejected and NOT persisted.
        record.state = State::Translating;
        QVERIFY(!m_store->updateJob(record).ok());
        QCOMPARE(m_store->loadJob(record.id)->state, State::Validating);

        // Next mainline step → accepted.
        record.state = State::Ingesting;
        QVERIFY(m_store->updateJob(record).ok());
        QCOMPARE(m_store->loadJob(record.id)->state, State::Ingesting);
    }

    void recoveryRoundTripKeepsPendingState()
    {
        auto record = *m_store->createJob(Kind::Render,
                                          QJsonObject{
                                              {QStringLiteral("script"),
                                               QStringLiteral("kịch bản mẫu")}})
                           .record;
        for (State s : {State::Validating, State::TtsRunning})
            QVERIFY(m_store->updateJob([&]{ record.state = s; return record; }()).ok());

        record.state = State::WaitingForProvider;
        QVERIFY(!m_store->updateJob(record).ok()); // pending missing

        record.pendingState = State::TtsRunning;
        QVERIFY(m_store->updateJob(record).ok());
        QCOMPARE(m_store->loadJob(record.id)->pendingState, State::TtsRunning);

        // Provider recovered → back to TTS_RUNNING; pending cleared on disk.
        record.state = State::TtsRunning;
        QVERIFY(m_store->updateJob(record).ok());
        const auto resumed = m_store->loadJob(record.id);
        QCOMPARE(resumed->state, State::TtsRunning);
        QVERIFY(!resumed->pendingState.has_value());
    }

    void corruptRecordFailsClosed()
    {
        const int healthyBefore = m_store->listJobs().size();

        const auto created = m_store->createJob(Kind::Render, {}).record;
        QVERIFY(created.has_value());

        // Corrupt the record on disk.
        QFile f(m_store->jobFilePath((*created).id));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"id\": \"x\", \"kind\": \"not-a-kind\"");
        f.close();

        QVERIFY(!m_store->loadJob((*created).id).has_value());

        // Corrupt entries are skipped, healthy ones untouched.
        QCOMPARE(m_store->listJobs().size(), healthyBefore);
    }

    void unknownJobUpdateRejected()
    {
        JobRecord ghost;
        ghost.id = QStringLiteral("nope");
        ghost.kind = Kind::Render;
        ghost.state = State::Validating;
        ghost.createdAtMs = 1;
        ghost.updatedAtMs = 1;
        QVERIFY(!m_store->updateJob(ghost).ok());
    }

private:
    QTemporaryDir m_dir;
    std::unique_ptr<JobStore> m_store;
};

QTEST_MAIN(TestJobStore)
#include "test_job_store.moc"
