#pragma once

#include <QJsonObject>
#include <QString>

#include "jobs/JobTypes.h"

namespace TtvStudio::Jobs {

// One persisted pipeline job. Serialized as <jobsRoot>/<id>/job.json.
struct JobRecord
{
    QString id;
    Kind kind = Kind::Render;
    State state = State::Created;

    // Set while sitting in a recovery state (WaitingForProvider /
    // UnknownProviderState): the mainline state to resume once the provider
    // resolves. Cleared on every non-recovery persistence.
    std::optional<State> pendingState;

    qint64 createdAtMs = 0;
    qint64 updatedAtMs = 0;
    QJsonObject params; // kind-specific input parameters

    QJsonObject toJson() const;
    // Returns std::nullopt for malformed JSON — callers must fail closed and
    // re-drive the stage rather than trust a partial record.
    static std::optional<JobRecord> fromJson(const class QJsonObject &obj);
};

} // namespace TtvStudio::Jobs
