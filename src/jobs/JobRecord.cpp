#include "jobs/JobRecord.h"

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>

namespace TtvStudio::Jobs {

QJsonObject JobRecord::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("kind"), kindToString(kind));
    obj.insert(QStringLiteral("state"), stateToString(state));
    if (pendingState.has_value())
        obj.insert(QStringLiteral("pending_state"), stateToString(*pendingState));
    obj.insert(QStringLiteral("created_at_ms"), createdAtMs);
    obj.insert(QStringLiteral("updated_at_ms"), updatedAtMs);
    obj.insert(QStringLiteral("params"), params);
    return obj;
}

std::optional<JobRecord> JobRecord::fromJson(const QJsonObject &obj)
{
    JobRecord record;

    record.id = obj.value(QStringLiteral("id")).toString();
    if (record.id.isEmpty())
        return std::nullopt;

    const auto kind =
        kindFromString(obj.value(QStringLiteral("kind")).toString());
    if (!kind.has_value())
        return std::nullopt;
    record.kind = *kind;

    const auto state =
        stateFromString(obj.value(QStringLiteral("state")).toString());
    if (!state.has_value())
        return std::nullopt;
    record.state = *state;

    const QString pendingText =
        obj.value(QStringLiteral("pending_state")).toString();
    if (!pendingText.isEmpty()) {
        const auto pending = stateFromString(pendingText);
        if (!pending.has_value())
            return std::nullopt;
        record.pendingState = *pending;
    }

    record.createdAtMs =
        static_cast<qint64>(obj.value(QStringLiteral("created_at_ms")).toDouble(0));
    record.updatedAtMs =
        static_cast<qint64>(obj.value(QStringLiteral("updated_at_ms")).toDouble(0));
    if (record.createdAtMs <= 0)
        return std::nullopt;

    record.params = obj.value(QStringLiteral("params")).toObject();
    return record;
}

} // namespace TtvStudio::Jobs
