#include "SceneTypes.h"

#include <QRegularExpression>

namespace TtvStudio::Render {

namespace {

const QRegularExpression &sceneIdPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^scene_\\d{3,}$"));
    return pattern;
}

QString cleanString(const QString &value)
{
    return value.trimmed();
}

} // namespace

QString sceneStatusToString(SceneStatus status)
{
    switch (status) {
    case SceneStatus::Planned: return QStringLiteral("planned");
    case SceneStatus::Submitted: return QStringLiteral("submitted");
    case SceneStatus::Running: return QStringLiteral("running");
    case SceneStatus::ClipReady: return QStringLiteral("clip_ready");
    case SceneStatus::Normalized: return QStringLiteral("normalized");
    case SceneStatus::FailedRetryable: return QStringLiteral("failed_retryable");
    case SceneStatus::FailedPermanent: return QStringLiteral("failed_permanent");
    }
    return QStringLiteral("planned");
}

std::optional<SceneStatus> sceneStatusFromString(const QString &text)
{
    if (text == QLatin1String("planned"))
        return SceneStatus::Planned;
    if (text == QLatin1String("submitted"))
        return SceneStatus::Submitted;
    if (text == QLatin1String("running"))
        return SceneStatus::Running;
    if (text == QLatin1String("clip_ready"))
        return SceneStatus::ClipReady;
    if (text == QLatin1String("normalized"))
        return SceneStatus::Normalized;
    if (text == QLatin1String("failed_retryable"))
        return SceneStatus::FailedRetryable;
    if (text == QLatin1String("failed_permanent"))
        return SceneStatus::FailedPermanent;
    return std::nullopt;
}

QJsonObject ContinuityContext::toJson() const
{
    QJsonObject obj;
    QJsonArray chars;
    for (const QString &c : characters)
        chars.append(c);
    obj.insert(QLatin1String("characters"), chars);
    obj.insert(QLatin1String("location"), location);
    obj.insert(QLatin1String("style"), style);
    return obj;
}

ContinuityContext ContinuityContext::fromJson(const QJsonObject &obj)
{
    ContinuityContext ctx;
    const auto chars = obj.value(QLatin1String("characters")).toArray();
    for (const auto &v : chars) {
        const QString name = v.toString().trimmed();
        if (!name.isEmpty())
            ctx.characters.append(name);
    }
    ctx.location = obj.value(QLatin1String("location")).toString();
    ctx.style = obj.value(QLatin1String("style")).toString();
    return ctx;
}

QJsonObject Scene::toJson() const
{
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("index"), index);
    obj.insert(QLatin1String("narration"), narration);
    obj.insert(QLatin1String("start_seconds"), startSeconds);
    obj.insert(QLatin1String("end_seconds"), endSeconds);
    obj.insert(QLatin1String("target_duration_seconds"), targetDurationSeconds);
    obj.insert(QLatin1String("generation_duration_seconds"), generationDurationSeconds);
    obj.insert(QLatin1String("visual_prompt"), visualPrompt);
    obj.insert(QLatin1String("continuity"), continuity.toJson());
    obj.insert(QLatin1String("provider_task_id"), providerTaskId);
    obj.insert(QLatin1String("raw_clip_path"), rawClipPath);
    obj.insert(QLatin1String("normalized_clip_path"), normalizedClipPath);
    obj.insert(QLatin1String("status"), sceneStatusToString(status));
    return obj;
}

bool Scene::fromJson(const QJsonObject &obj, Scene *out)
{
    *out = Scene{};
    out->id = cleanString(obj.value(QLatin1String("id")).toString());
    if (!sceneIdPattern().match(out->id).hasMatch())
        return false;

    const QJsonValue indexValue = obj.value(QLatin1String("index"));
    if (!indexValue.isDouble())
        return false;
    out->index = int(indexValue.toDouble());

    out->narration = obj.value(QLatin1String("narration")).toString();
    if (out->narration.trimmed().isEmpty())
        return false;

    const auto readPositive = [&obj](const char *key, double *target) {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (!v.isDouble())
            return false;
        *target = v.toDouble();
        return *target > 0.0;
    };

    if (!readPositive("end_seconds", &out->endSeconds))
        return false;
    const QJsonValue start = obj.value(QLatin1String("start_seconds"));
    if (!start.isDouble())
        return false;
    out->startSeconds = start.toDouble();

    if (!readPositive("target_duration_seconds", &out->targetDurationSeconds))
        return false;
    if (!readPositive("generation_duration_seconds", &out->generationDurationSeconds))
        return false;

    out->visualPrompt = obj.value(QLatin1String("visual_prompt")).toString().trimmed();
    if (out->visualPrompt.isEmpty())
        return false;

    const QJsonValue continuity = obj.value(QLatin1String("continuity"));
    if (continuity.isObject())
        out->continuity = ContinuityContext::fromJson(continuity.toObject());

    out->providerTaskId = obj.value(QLatin1String("provider_task_id")).toString();
    out->rawClipPath = obj.value(QLatin1String("raw_clip_path")).toString();
    out->normalizedClipPath = obj.value(QLatin1String("normalized_clip_path")).toString();

    const QString statusText = obj.value(QLatin1String("status")).toString();
    if (statusText.isEmpty()) {
        out->status = SceneStatus::Planned;
    } else {
        const auto status = sceneStatusFromString(statusText);
        if (!status)
            return false;
        out->status = *status;
    }

    if (out->endSeconds <= out->startSeconds)
        return false;
    return true;
}

QJsonObject ScenePlan::toJson() const
{
    QJsonObject obj;
    QJsonArray arr;
    for (const Scene &scene : scenes)
        arr.append(scene.toJson());
    obj.insert(QLatin1String("scenes"), arr);
    obj.insert(QLatin1String("total_duration_seconds"), totalDurationSeconds);
    return obj;
}

bool ScenePlan::fromJson(const QJsonObject &obj, ScenePlan *out)
{
    *out = ScenePlan{};
    const auto arr = obj.value(QLatin1String("scenes")).toArray();
    if (arr.isEmpty())
        return false;

    out->scenes.reserve(arr.size());
    for (const auto &v : arr) {
        Scene scene;
        if (!v.isObject() || !Scene::fromJson(v.toObject(), &scene))
            return false;
        out->scenes.append(scene);
    }
    const QJsonValue total = obj.value(QLatin1String("total_duration_seconds"));
    if (!total.isDouble())
        return false;
    out->totalDurationSeconds = total.toDouble();
    return true;
}

} // namespace TtvStudio::Render
