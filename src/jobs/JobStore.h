#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QJsonObject>

#include "jobs/JobRecord.h"
#include "jobs/JobTypes.h"

namespace TtvStudio::Jobs {

// Unified outcome for mutating store operations.
struct StoreResult
{
    std::optional<JobRecord> record;
    QString error;

    bool ok() const { return record.has_value(); }
};

// Durable, restart-safe job registry rooted at Paths::jobsRoot().
// Every mutation rewrites job.json atomically (write .part → rename) so a
// crash can never leave a half-written record behind.
class JobStore : public QObject
{
    Q_OBJECT

public:
    explicit JobStore(QObject *parent = nullptr);

    // Sets and creates the storage root. Returns false when the directory
    // cannot be created (unwritable volume etc.).
    bool setRoot(const QString &rootPath);
    const QString &rootPath() const { return m_rootPath; }

    // Creates the artifact skeleton (input/, work/, output/) + initial
    // CREATED record. Fails when the id already exists.
    StoreResult createJob(Kind kind, const QJsonObject &params,
                          const QString &explicitId = {});

    QList<JobRecord> listJobs() const;

    std::optional<JobRecord> loadJob(const QString &jobId) const;

    // Validates the state transition against the persisted record, stamps
    // updated_at_ms and persists atomically.
    StoreResult updateJob(const JobRecord &updated);

    QString jobDir(const QString &jobId) const;
    QString jobFilePath(const QString &jobId) const;

private:
    QString m_rootPath;
};

} // namespace TtvStudio::Jobs
