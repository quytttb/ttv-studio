#pragma once

#include <optional>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace TtvStudio::Render {

// Lifecycle of one generated scene clip (subset of the provider contract the
// render pipeline needs; mirrors scenes.json "status").
enum class SceneStatus
{
    Planned,
    Submitted,
    Running,
    ClipReady,        // raw clip downloaded
    Normalized,       // fitted to target duration/fps/codec
    FailedRetryable,
    FailedPermanent
};

QString sceneStatusToString(SceneStatus status);
std::optional<SceneStatus> sceneStatusFromString(const QString &text);

// Shared visual continuity hints carried between adjacent scenes.
struct ContinuityContext
{
    QStringList characters;
    QString location;
    QString style;

    QJsonObject toJson() const;
    static ContinuityContext fromJson(const QJsonObject &obj);
};

// One narration segment mapped to a single generated clip.
struct Scene
{
    QString id;       // "scene_001"
    int index = 0;    // 1-based
    QString narration;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    double targetDurationSeconds = 0.0;      // exact window in the master timeline
    double generationDurationSeconds = 0.0;  // discrete duration requested from the provider
    QString visualPrompt;
    ContinuityContext continuity;
    QString providerTaskId;                  // durable gateway task id (resume)
    QString rawClipPath;                     // relative to the job dir
    QString normalizedClipPath;              // relative to the job dir
    SceneStatus status = SceneStatus::Planned;

    double durationSeconds() const { return endSeconds - startSeconds; }

    QJsonObject toJson() const;
    // Returns false and clears *out* on malformed input — callers fail closed.
    static bool fromJson(const QJsonObject &obj, Scene *out);
};

struct ScenePlan
{
    QVector<Scene> scenes;
    double totalDurationSeconds = 0.0;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &obj, ScenePlan *out);
};

} // namespace TtvStudio::Render
