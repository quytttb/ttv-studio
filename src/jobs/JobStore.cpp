#include "jobs/JobStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

#include "utils/Paths.h"

namespace TtvStudio::Jobs {

namespace {

// Atomic JSON write: serialize → write "<path>.part" → rename over target.
// On POSIX/NTFS rename over an existing file is atomic; a crash can leave the
// .part behind but never a torn job.json.
bool writeJsonAtomic(const QString &targetPath, const QJsonObject &obj,
                     QString *error)
{
    const QByteArray payload =
        QJsonDocument(obj).toJson(QJsonDocument::Indented);

    const QString partPath = targetPath + QStringLiteral(".part");
    QFile partFile(partPath);
    if (!partFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("cannot open %1: %2")
                         .arg(partPath, partFile.errorString());
        }
        return false;
    }
    if (partFile.write(payload) != payload.size()) {
        if (error)
            *error = QStringLiteral("short write on %1").arg(partPath);
        partFile.close();
        QFile::remove(partPath);
        return false;
    }
    partFile.close();

    // Remove a stale leftover from a previous crash before renaming.
    QFile::remove(targetPath);
    if (!QFile::rename(partPath, targetPath)) {
        if (error)
            *error = QStringLiteral("rename %1 -> %2 failed")
                         .arg(partPath, targetPath);
        QFile::remove(partPath);
        return false;
    }
    return true;
}

} // namespace

JobStore::JobStore(QObject *parent)
    : QObject(parent)
{
}

bool JobStore::setRoot(const QString &rootPath)
{
    m_rootPath = QDir(rootPath).absolutePath();
    QDir dir(m_rootPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return false;

    // Artifact skeleton shared by every job kind.
    for (const QString sub : {QStringLiteral("input"), QStringLiteral("work"),
                              QStringLiteral("output")}) {
        // Subdirectories are created per job; nothing needed at root level.
        Q_UNUSED(sub);
    }
    return true;
}

QString JobStore::jobDir(const QString &jobId) const
{
    return m_rootPath + QLatin1Char('/') + jobId;
}

QString JobStore::jobFilePath(const QString &jobId) const
{
    return jobDir(jobId) + QStringLiteral("/job.json");
}

StoreResult JobStore::createJob(Kind kind, const QJsonObject &params,
                                const QString &explicitId)
{
    StoreResult result;

    QString jobId = explicitId;
    if (jobId.isEmpty())
        jobId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

    QDir dir(jobDir(jobId));
    if (dir.exists()) {
        result.error = QStringLiteral("job id already exists: %1").arg(jobId);
        return result;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    JobRecord record;
    record.id = jobId;
    record.kind = kind;
    record.state = State::Created;
    record.createdAtMs = now;
    record.updatedAtMs = now;
    record.params = params;

    if (!dir.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("cannot create %1").arg(dir.absolutePath());
        return result;
    }
    for (const QString sub : {QStringLiteral("input"), QStringLiteral("work"),
                              QStringLiteral("output")}) {
        if (!dir.mkpath(sub)) {
            result.error = QStringLiteral("cannot create %1/%2")
                               .arg(dir.absolutePath(), sub);
            return result;
        }
    }

    QString writeError;
    if (!writeJsonAtomic(jobFilePath(jobId), record.toJson(), &writeError)) {
        result.error = writeError;
        return result;
    }

    result.record = record;
    return result;
}

std::optional<JobRecord> JobStore::loadJob(const QString &jobId) const
{
    QFile file(jobFilePath(jobId));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;

    return JobRecord::fromJson(doc.object());
}

QList<JobRecord> JobStore::listJobs() const
{
    QList<JobRecord> jobs;

    QDir root(m_rootPath);
    const auto entries =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        auto record = loadJob(entry);
        if (record.has_value())
            jobs.append(*record);
    }
    return jobs;
}

StoreResult JobStore::updateJob(const JobRecord &updated)
{
    StoreResult result;

    const auto storedOpt = loadJob(updated.id);
    if (!storedOpt.has_value()) {
        result.error = QStringLiteral("unknown or unreadable job: %1")
                           .arg(updated.id);
        return result;
    }

    const JobRecord &stored = *storedOpt;

    // Validate the transition, including recovery resume semantics: when the
    // stored state is a recovery state, only its recorded pendingState (or an
    // abort) may follow.
    if (updated.state != stored.state) {
        const bool allowed =
            isRecovery(stored.state)
                ? canTransition(stored.kind, stored.state, updated.state,
                                updated.pendingState)
                : canTransition(stored.kind, stored.state, updated.state);
        if (!allowed) {
            result.error = QStringLiteral(
                               "illegal transition %1: %2 -> %3")
                               .arg(kindToString(stored.kind),
                                    stateToString(stored.state),
                                    stateToString(updated.state));
            return result;
        }
    } else if (isRecovery(stored.state)
               && updated.pendingState != stored.pendingState) {
        result.error = QStringLiteral(
                           "pending state of a recovery job is immutable");
        return result;
    }

    // A persisted record may never sit in a recovery state without recording
    // where the pipeline should resume.
    if (isRecovery(updated.state) && !updated.pendingState.has_value()) {
        result.error = QStringLiteral(
                           "state %1 requires pending_state")
                           .arg(stateToString(updated.state));
        return result;
    }

    JobRecord toPersist = updated;
    if (!isRecovery(toPersist.state))
        toPersist.pendingState.reset(); // cleared once we leave recovery

    toPersist.updatedAtMs = QDateTime::currentMSecsSinceEpoch();

    QString writeError;
    if (!writeJsonAtomic(jobFilePath(toPersist.id), toPersist.toJson(),
                         &writeError)) {
        result.error = writeError;
        return result;
    }

    result.record = toPersist;
    return result;
}

} // namespace TtvStudio::Jobs
