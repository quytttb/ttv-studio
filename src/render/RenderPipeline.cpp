#include "RenderPipeline.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>

#include "media/HardwareEncoder.h"
#include "media/MediaEngine.h"
#include "render/ScenePlanner.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Render {

namespace Jobs = TtvStudio::Jobs;
using Providers::ProviderError;

namespace {

constexpr int kPollSleepChunkMs = 100; // cancel-responsive sleeping

qint64 jitterBetween(int minMs, int maxMs)
{
    const double u = QRandomGenerator::system()->generateDouble();
    return qint64(minMs + u * qMax(maxMs - minMs, 0));
}

QString sceneRawClipRelative(int index)
{
    return QStringLiteral("clips/raw/%1.mp4").arg(index, 3, 10, QChar('0'));
}

QString sceneNormalizedClipRelative(int index)
{
    return QStringLiteral("clips/normalized/%1.mp4").arg(index, 3, 10, QChar('0'));
}

bool writeFileAtomically(const QString &destination, const QByteArray &content, QString *error)
{
    // QSaveFile commits via an atomic rename that also REPLACES an existing
    // destination (plain QFile::rename refuses to overwrite).
    QSaveFile part(destination);
    if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1").arg(destination);
        return false;
    }
    if (part.write(content) != content.size() || !part.commit()) {
        part.cancelWriting();
        *error = QStringLiteral("cannot publish %1").arg(destination);
        return false;
    }
    return true;
}

bool readFileIfExists(const QString &path, QByteArray *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.readAll();
    return true;
}

} // namespace

QString runOutcomeToString(RunOutcome outcome)
{
    switch (outcome) {
    case RunOutcome::Completed: return QStringLiteral("completed");
    case RunOutcome::WaitingForProvider: return QStringLiteral("waiting_for_provider");
    case RunOutcome::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

RenderPipeline::Paths RenderPipeline::Paths::forJob(const QString &jobDir)
{
    Paths p;
    p.root = jobDir;
    p.scriptTxt = jobDir + QStringLiteral("/input/script.txt");
    p.masterAudio = jobDir + QStringLiteral("/audio/master.wav");
    p.scenesJson = jobDir + QStringLiteral("/timeline/scenes.json");
    p.captionsVtt = jobDir + QStringLiteral("/timeline/captions.vtt");
    p.concatMp4 = jobDir + QStringLiteral("/work/concat.mp4");
    p.finalMp4 = jobDir + QStringLiteral("/output/final_video.mp4");
    return p;
}

QString RenderPipeline::Paths::rawClip(int sceneIndex) const
{
    return root + QLatin1Char('/') + sceneRawClipRelative(sceneIndex);
}

QString RenderPipeline::Paths::normalizedClip(int sceneIndex) const
{
    return root + QLatin1Char('/') + sceneNormalizedClipRelative(sceneIndex);
}

RenderPipeline::RenderPipeline(Jobs::JobStore &store,
                               Providers::TtsClient &tts,
                               Providers::LlmClient &llm,
                               Providers::VideoGatewayClient &video,
                               const Media::Ffprobe &ffprobe,
                               const QString &ffmpegBin,
                               RenderPipelineConfig config,
                               std::function<void(qint64 ms)> sleepFn)
    : m_store(store),
      m_tts(tts),
      m_llm(llm),
      m_video(video),
      m_ffprobe(ffprobe),
      m_ffmpegBin(ffmpegBin),
      m_config(std::move(config)),
      m_sleep(std::move(sleepFn))
{
    if (!m_sleep) // production default; tests inject an instant no-op
        m_sleep = [](qint64 ms) { QThread::msleep(quint64(ms)); };
}

void RenderPipeline::requestCancel()
{
    m_cancelRequested.store(true);
}

void RenderPipeline::emitStage(const QString &jobId, Jobs::State state, const QString &message)
{
    Q_EMIT stageChanged(jobId, Jobs::stateToString(state), message);
}

bool RenderPipeline::transition(Jobs::JobRecord record, Jobs::State to,
                                const QString &message, QString *error)
{
    const Jobs::State from = record.state;
    // Keep any recorded pendingState: JobStore validates recovery resumes
    // against it and clears it itself once persisted outside recovery states.
    record.state = to;
    const auto result = m_store.updateJob(record);
    if (!result.ok()) {
        *error = QStringLiteral("transition %1 → %2 rejected: %3")
                     .arg(Jobs::stateToString(from), Jobs::stateToString(to), result.error);
        return false;
    }
    emitStage(result.record->id, to, message);
    return true;
}

bool RenderPipeline::failJob(const QString &jobId, const QString &message, QString *error)
{
    auto record = m_store.loadJob(jobId);
    if (record) {
        record->pendingState = std::nullopt;
        record->state = Jobs::State::Failed;
        (void)m_store.updateJob(*record);
    }
    *error = message;
    emitStage(jobId, Jobs::State::Failed, message);
    return false;
}

bool RenderPipeline::cancelled(const QString &jobId, QString *error)
{
    if (!m_cancelRequested.load())
        return false;
    auto record = m_store.loadJob(jobId);
    if (record && !Jobs::isTerminal(record->state)) {
        record->pendingState = std::nullopt;
        record->state = Jobs::State::Cancelled;
        (void)m_store.updateJob(*record);
        emitStage(jobId, Jobs::State::Cancelled, QStringLiteral("cancelled by user"));
    }
    *error = QStringLiteral("cancelled");
    return true;
}

void RenderPipeline::sleepChunked(qint64 totalMs)
{
    qint64 waited = 0;
    while (waited < totalMs && !m_cancelRequested.load()) {
        const qint64 chunk = qMin<qint64>(kPollSleepChunkMs, totalMs - waited);
        m_sleep(chunk);
        waited += chunk;
    }
}

bool RenderPipeline::validateStage(const Jobs::JobRecord &record, const Paths &paths,
                                   const QJsonObject &params, QString *error)
{
    const QString script = params.value(QLatin1String("script_text")).toString().trimmed();
    if (script.isEmpty()) {
        *error = QStringLiteral("script_text must not be empty");
        return false;
    }
    // Persist the canonical script for provenance/resume.
    QDir().mkpath(QFileInfo(paths.scriptTxt).absolutePath());
    return writeFileAtomically(paths.scriptTxt, script.toUtf8(), error);
}

bool RenderPipeline::ttsStage(const Jobs::JobRecord &record, const QJsonObject &params,
                              const Paths &paths, double *audioDurationOut, QString *error)
{
    Q_UNUSED(record);

    Providers::TtsRequest request;
    request.text = params.value(QLatin1String("script_text")).toString();
    request.language =
        params.value(QLatin1String("language")).toString(m_config.language);
    request.profileId = params.value(QLatin1String("tts_profile_id")).toString();
    request.instruct = params.value(QLatin1String("tts_instruct")).toString();

    const auto result = m_tts.synthesize(request, paths.masterAudio);
    if (!result.ok) {
        *error = result.error.message;
        return false;
    }

    QDir().mkpath(QFileInfo(paths.captionsVtt).absolutePath());
    if (!writeFileAtomically(paths.captionsVtt,
                             renderVtt(proportionalCues(request.text, result.durationSec))
                                 .toUtf8(),
                             error)) {
        return false;
    }
    *audioDurationOut = result.durationSec;
    return true;
}

bool RenderPipeline::planningStage(const Jobs::JobRecord &record, const QJsonObject &params,
                                   const Paths &paths, double audioDuration,
                                   ScenePlan *planOut, QString *error)
{
    Q_UNUSED(record);

    const ScenePlanner planner{m_llm};
    const PlanOutcome outcome =
        planner.plan(params.value(QLatin1String("script_text")).toString(), m_config.language,
                     audioDuration);
    if (!outcome.ok) {
        *error = outcome.error;
        return false;
    }

    SceneManifestError manifestError;
    const ScenePlan plan =
        buildScenePlan(outcome.proposals, audioDuration, m_config.supportedGenerationDurations,
                       m_config.maxRetimeFactor, &manifestError);
    if (!manifestError.message.isEmpty()) {
        *error = manifestError.message;
        return false;
    }

    *planOut = plan;
    QDir().mkpath(QFileInfo(paths.scenesJson).absolutePath());
    return persistPlan(paths, *planOut, error);
}

bool RenderPipeline::loadPlan(const Paths &paths, ScenePlan *planOut, QString *error)
{
    QByteArray content;
    if (!readFileIfExists(paths.scenesJson, &content)) {
        *error = QStringLiteral("scene manifest missing: %1").arg(paths.scenesJson);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(content);
    if (!doc.isObject() || !ScenePlan::fromJson(doc.object(), planOut)) {
        *error = QStringLiteral("scene manifest is malformed: %1").arg(paths.scenesJson);
        return false;
    }
    return true;
}

bool RenderPipeline::persistPlan(const Paths &paths, const ScenePlan &plan, QString *error)
{
    QString localError;
    const bool ok = writeFileAtomically(
        paths.scenesJson, QJsonDocument(plan.toJson()).toJson(QJsonDocument::Indented),
        &localError);
    if (!ok)
        qWarning("RenderPipeline: cannot persist scene manifest: %s", qUtf8Printable(localError));
    return ok;
}

void RenderPipeline::generatePromptForScene(const Scene &scene, const ScenePlan &plan,
                                            QString *promptOut) const
{
    const Scene *previous = nullptr;
    const Scene *following = nullptr;
    for (const Scene &candidate : plan.scenes) {
        if (candidate.index == scene.index - 1)
            previous = &candidate;
        else if (candidate.index == scene.index + 1)
            following = &candidate;
    }

    QStringList parts{scene.visualPrompt};
    QStringList notes;
    for (const Scene *neighbor : {previous, following}) {
        if (!neighbor || scene.continuity.location.isEmpty())
            continue;
        if (!neighbor->continuity.location.isEmpty()
            && neighbor->continuity.location == scene.continuity.location) {
            notes.append(QStringLiteral("same location as %1: %2")
                             .arg(neighbor->index < scene.index
                                      ? QStringLiteral("previous scene")
                                      : QStringLiteral("next scene"),
                                  scene.continuity.location));
        }
    }
    if (!scene.continuity.style.isEmpty())
        notes.append(QStringLiteral("style: %1").arg(scene.continuity.style));
    if (!scene.continuity.characters.isEmpty())
        notes.append(QStringLiteral("characters: ")
                     + scene.continuity.characters.join(QStringLiteral(", ")));
    if (!notes.isEmpty())
        parts.append(QStringLiteral("Continuity: %1.")
                         .arg(notes.join(QStringLiteral("; "))));
    *promptOut = parts.join(QChar(' '));
}

RenderPipeline::ClipVerdict RenderPipeline::processScene(Scene *scene, const QString &prompt,
                                                         const Paths &paths, QString *error)
{
    int submissions = 0;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + m_config.videoTaskBudgetMs;

    forever {
        if (m_cancelRequested.load()) {
            *error = QStringLiteral("cancelled");
            return ClipVerdict::PermanentFailure;
        }

        // Phase A — obtain a durable provider task id ("never pay twice").
        if (scene->providerTaskId.isEmpty()) {
            if (++submissions > m_config.maxSubmissionsPerScene) {
                *error = QStringLiteral("scene %1 exhausted its submission budget")
                             .arg(scene->index);
                return ClipVerdict::PermanentFailure;
            }
            Providers::VideoSubmitRequest request;
            request.prompt = prompt;
            request.durationSeconds = int(qRound(scene->generationDurationSeconds));
            const auto submitted = m_video.submit(request);
            if (!submitted.ok) {
                *error = submitted.error.message;
                if (submitted.error.kind == Providers::ErrorKind::Permanent)
                    return ClipVerdict::PermanentFailure;
                // Transient / ambiguous: retry within the same budget.
            } else {
                scene->providerTaskId = submitted.taskId;
                scene->status = SceneStatus::Submitted;
                // Caller persists the manifest right away so a crash here can
                // never orphan a paid-for task.
            }
        } else {
            // Phase B — reconcile the live task until terminal or out of budget.
            const auto polled = m_video.poll(scene->providerTaskId);
            if (polled.ok) {
                switch (polled.state) {
                case Providers::VideoTaskState::Succeeded: {
                    if (polled.mediaUrls.isEmpty()) {
                        *error = QStringLiteral("task %1 succeeded without media urls")
                                     .arg(scene->providerTaskId);
                        return ClipVerdict::PermanentFailure;
                    }
                    const auto downloaded =
                        m_video.download(QUrl(polled.mediaUrls.first()),
                                         paths.rawClip(scene->index),
                                         Defaults::kVideoMaxDownloadBytes);
                    if (downloaded.ok) {
                        scene->rawClipPath = sceneRawClipRelative(scene->index);
                        scene->status = SceneStatus::ClipReady;
                        return ClipVerdict::Ready;
                    }
                    *error = downloaded.error.message;
                    if (downloaded.error.kind == Providers::ErrorKind::Permanent)
                        return ClipVerdict::PermanentFailure;
                    break; // transient — keep waiting/retrying within budget
                }
                case Providers::VideoTaskState::FailedPermanent:
                    scene->status = SceneStatus::FailedPermanent;
                    *error = QStringLiteral("task %1 failed permanently: %2")
                                 .arg(scene->providerTaskId, polled.errorMessage);
                    return ClipVerdict::PermanentFailure;
                case Providers::VideoTaskState::FailedRetryable:
                    // Burn this task; the next iteration submits fresh (the
                    // per-scene submission budget bounds the loop).
                    scene->providerTaskId.clear();
                    scene->status = SceneStatus::FailedRetryable;
                    break;
                case Providers::VideoTaskState::Submitted:
                case Providers::VideoTaskState::Running:
                case Providers::VideoTaskState::Unknown:
                    break;
                }
            } else if (polled.error.kind == Providers::ErrorKind::Permanent) {
                *error = polled.error.message;
                return ClipVerdict::PermanentFailure;
            }
            // Transient / ambiguous poll errors fall through to the wait.
        }

        if (QDateTime::currentMSecsSinceEpoch() >= deadline) {
            *error = QStringLiteral("scene %1 did not finish within the provider budget")
                         .arg(scene->index);
            return ClipVerdict::Outstanding;
        }
        sleepChunked(jitterBetween(m_config.videoPollMinMs, m_config.videoPollMaxMs));
        if (m_cancelRequested.load()) {
            *error = QStringLiteral("cancelled");
            return ClipVerdict::PermanentFailure;
        }
    }
}

bool RenderPipeline::videoStage(const Jobs::JobRecord &record, const Paths &paths,
                                ScenePlan *plan, RunOutcome *outcome, QString *error)
{
    int done = 0;
    bool anyOutstanding = false;

    for (int i = 0; i < plan->scenes.size(); ++i) {
        Scene &scene = plan->scenes[i];
        const QString rawAbs = paths.rawClip(scene.index);

        const bool clipValid =
            (scene.status == SceneStatus::ClipReady || scene.status == SceneStatus::Normalized)
            && QFile::exists(rawAbs);
        if (clipValid) {
            ++done;
            continue;
        }
        if (scene.status == SceneStatus::FailedPermanent) {
            failJob(record.id,
                    QStringLiteral("scene %1 failed permanently in a previous run")
                        .arg(scene.index),
                    error);
            *outcome = RunOutcome::Failed;
            return false;
        }

        QString prompt;
        generatePromptForScene(scene, *plan, &prompt);

        QString sceneError;
        const ClipVerdict verdict = processScene(&scene, prompt, paths, &sceneError);
        persistPlan(paths, *plan, error); // durable after every transition

        if (verdict == ClipVerdict::PermanentFailure) {
            scene.status = SceneStatus::FailedPermanent;
            persistPlan(paths, *plan, error);
            failJob(record.id, QStringLiteral("scene %1: %2").arg(scene.index).arg(sceneError),
                    error);
            *outcome = RunOutcome::Failed;
            return false;
        }
        if (verdict == ClipVerdict::Outstanding) {
            anyOutstanding = true;
            continue; // give the remaining scenes their chance this run
        }
        ++done;
        Q_EMIT sceneProgress(record.id, done, plan->scenes.size());
    }

    if (anyOutstanding) {
        auto pending = m_store.loadJob(record.id);
        if (!pending) {
            *error = QStringLiteral("job vanished during video stage");
            return false;
        }
        pending->pendingState = Jobs::State::VideoRunning;
        pending->state = Jobs::State::WaitingForProvider;
        const auto result = m_store.updateJob(*pending);
        if (!result.ok()) {
            *error = result.error;
            return false;
        }
        emitStage(record.id, Jobs::State::WaitingForProvider,
                  QStringLiteral("provider tasks outstanding"));
        *outcome = RunOutcome::WaitingForProvider;
        return true;
    }

    auto refreshed = m_store.loadJob(record.id);
    if (!refreshed) {
        *error = QStringLiteral("job vanished during video stage");
        return false;
    }
    if (!transition(*refreshed, Jobs::State::ClipsReady,
                    QStringLiteral("all scene clips ready"), error)) {
        return false;
    }
    *outcome = RunOutcome::Completed;
    return true;
}

bool RenderPipeline::postProcessingStage(const Paths &paths, ScenePlan *plan,
                                         double audioDuration, QString *error)
{
    Q_UNUSED(audioDuration);

    Media::MediaEngine engine{m_ffmpegBin, &m_ffprobe};
    Media::NormalizeTarget target;
    target.fps = m_config.normalizeFps;

    const QStringList encoderArgs =
        Media::HardwareEncoder::encodingArgs(m_config.videoBackend);
    // encoderArgs = ["-c:v", <codec>, <quality flags…>]
    const Media::VideoEncodeConfig encode{encoderArgs.at(1), encoderArgs.mid(2)};

    for (Scene &scene : plan->scenes) {
        if (scene.status == SceneStatus::Normalized
            && QFile::exists(paths.normalizedClip(scene.index)))
            continue;

        const QString rawAbs = paths.rawClip(scene.index);
        const auto probeInfo = m_ffprobe.probe(rawAbs);
        if (!probeInfo) {
            *error = QStringLiteral("ffprobe failed on raw clip for scene %1").arg(scene.index);
            return false;
        }

        Media::FitPlanError fitError;
        const auto decision =
            Media::planFit(probeInfo->durationSec, scene.targetDurationSeconds, target.fps,
                           m_config.maxRetimeFactor, m_config.maxFreezeSeconds, &fitError);
        if (!decision) {
            *error = QStringLiteral("scene %1: %2").arg(scene.index).arg(fitError.message);
            return false;
        }

        QString engineError;
        if (!engine.fitClip(rawAbs, paths.normalizedClip(scene.index), target, *decision,
                            encode, &engineError)) {
            *error = QStringLiteral("scene %1: %2").arg(scene.index).arg(engineError);
            return false;
        }
        scene.normalizedClipPath = sceneNormalizedClipRelative(scene.index);
        scene.status = SceneStatus::Normalized;
        persistPlan(paths, *plan, error); // crash-safe incremental progress
    }

    QStringList normalizedClips;
    for (const Scene &scene : std::as_const(plan->scenes))
        normalizedClips.append(paths.normalizedClip(scene.index));

    QString engineError;
    if (!engine.concatClips(normalizedClips, paths.concatMp4, encode, &engineError)) {
        *error = engineError;
        return false;
    }

    const QString finalPart = paths.finalMp4 + QStringLiteral(".part");
    if (!engine.muxNarration(paths.concatMp4, paths.masterAudio, finalPart,
                             m_config.audioBitrateKbps, &engineError)) {
        QFile::remove(finalPart);
        *error = engineError;
        return false;
    }
    if (!QFile::rename(finalPart, paths.finalMp4)) {
        QFile::remove(finalPart);
        *error = QStringLiteral("cannot publish final video");
        return false;
    }
    return true;
}

bool RenderPipeline::verifyingStage(const Paths &paths, double audioDuration, QString *error)
{
    const auto info = m_ffprobe.probe(paths.finalMp4);
    if (!info) {
        *error = QStringLiteral("final video failed ffprobe validation");
        return false;
    }
    if (!info->hasVideo || !info->hasAudio) {
        *error = QStringLiteral("final video is missing %1 stream")
                     .arg(info->hasVideo ? QStringLiteral("audio")
                                         : QStringLiteral("video"));
        return false;
    }
    if (audioDuration > 0.0 && info->durationSec < 0.5 * audioDuration) {
        *error = QStringLiteral("final video duration %1s is implausibly short vs narration %2s")
                     .arg(info->durationSec, 0, 'f', 2)
                     .arg(audioDuration, 0, 'f', 2);
        return false;
    }
    return true;
}

RunOutcome RenderPipeline::runJob(const QString &jobId, QString *error)
{
    m_cancelRequested.store(false);

    const auto initial = m_store.loadJob(jobId);
    if (!initial) {
        *error = QStringLiteral("job not found: %1").arg(jobId);
        return RunOutcome::Failed;
    }
    if (Jobs::isTerminal(initial->state)) {
        *error = QStringLiteral("job %1 is terminal (%2)")
                     .arg(jobId, Jobs::stateToString(initial->state));
        return RunOutcome::Failed;
    }

    const Paths paths = Paths::forJob(m_store.jobDir(jobId));

    // Ensure the artifact layout beyond the store's input/work/output
    // skeleton (ffmpeg never creates directories, neither do our writers).
    QDir().mkpath(QFileInfo(paths.masterAudio).absolutePath());
    QDir().mkpath(QFileInfo(paths.scenesJson).absolutePath());
    QDir().mkpath(paths.root + QStringLiteral("/clips/raw"));
    QDir().mkpath(paths.root + QStringLiteral("/clips/normalized"));

    // --- VALIDATING ---------------------------------------------------------
    if (initial->state == Jobs::State::Created) {
        if (!validateStage(*initial, paths, initial->params, error))
        {
            failJob(jobId, QStringLiteral("validate: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*initial, Jobs::State::Validating, QStringLiteral("job accepted"), error))
            return RunOutcome::Failed;
    }

    auto record = m_store.loadJob(jobId);
    if (!record) {
        *error = QStringLiteral("job vanished mid-run");
        return RunOutcome::Failed;
    }

    // --- TTS ------------------------------------------------------------------
    double audioDuration = 0.0;
    if (record->state == Jobs::State::Validating) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::TtsRunning,
                        QStringLiteral("narration synthesis started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (record->state == Jobs::State::TtsRunning) {
        if (!ttsStage(*record, record->params, paths, &audioDuration, error))
        {
            failJob(jobId, QStringLiteral("tts: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::TtsReady,
                        QStringLiteral("master narration ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (audioDuration <= 0.0) {
        const auto probeInfo = m_ffprobe.probe(paths.masterAudio);
        if (!probeInfo) {
            failJob(jobId, QStringLiteral("master narration missing or unreadable"), error);
            return RunOutcome::Failed;
        }
        audioDuration = probeInfo->durationSec;
    }

    // --- PLANNING ---------------------------------------------------------------
    ScenePlan plan;
    if (record->state == Jobs::State::TtsReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::Planning,
                        QStringLiteral("scene planning started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (record->state == Jobs::State::Planning) {
        if (!planningStage(*record, record->params, paths, audioDuration, &plan, error))
        {
            failJob(jobId, QStringLiteral("planning: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::ScenesReady,
                        QStringLiteral("scene manifest ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (plan.scenes.isEmpty() && !loadPlan(paths, &plan, error))
        return RunOutcome::Failed;

    // Resume from recovery: leaving WaitingForProvider requires the recorded
    // pendingState as the exact target.
    if (record->state == Jobs::State::WaitingForProvider) {
        if (record->pendingState != Jobs::State::VideoRunning) {
            *error = QStringLiteral("recovery state records an unexpected pending state");
            return RunOutcome::Failed;
        }
        Jobs::JobRecord pending = *record;
        if (!transition(pending, Jobs::State::VideoRunning,
                        QStringLiteral("video generation resumed"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }

    // --- VIDEO -----------------------------------------------------------------
    if (record->state == Jobs::State::ScenesReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::VideoRunning,
                        QStringLiteral("video generation started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (record->state == Jobs::State::VideoRunning) {
        RunOutcome videoOutcome = RunOutcome::Completed;
        if (!videoStage(*record, paths, &plan, &videoOutcome, error))
            return RunOutcome::Failed;
        if (videoOutcome == RunOutcome::WaitingForProvider)
            return videoOutcome;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }

    // --- POST-PROCESSING -------------------------------------------------------
    if (record->state == Jobs::State::ClipsReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::PostProcessing,
                        QStringLiteral("post-production started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }
    if (record->state == Jobs::State::PostProcessing) {
        if (!postProcessingStage(paths, &plan, audioDuration, error))
        {
            failJob(jobId, QStringLiteral("post-processing: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(*record, Jobs::State::Verifying,
                        QStringLiteral("output verification started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }

    // --- VERIFYING ---------------------------------------------------------------
    if (record->state == Jobs::State::Verifying) {
        if (!verifyingStage(paths, audioDuration, error))
        {
            failJob(jobId, QStringLiteral("verify: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (!transition(*record, Jobs::State::Completed, QStringLiteral("render completed"),
                        error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId);
        if (!record)
            return RunOutcome::Failed;
    }

    if (record->state != Jobs::State::Completed) {
        *error = QStringLiteral("unexpected stage after verify: %1")
                     .arg(Jobs::stateToString(record->state));
        return RunOutcome::Failed;
    }
    return RunOutcome::Completed;
}

} // namespace TtvStudio::Render
