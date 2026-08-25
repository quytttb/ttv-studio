#pragma once

#include <atomic>

#include <QObject>
#include <QString>

#include "jobs/JobStore.h"
#include "media/Ffprobe.h"
#include "providers/LlmClient.h"
#include "providers/TtsClient.h"
#include "providers/VideoGatewayClient.h"
#include "render/Captions.h"
#include "render/SceneManifest.h"

namespace TtvStudio::Media {
class MediaEngine;
}

namespace TtvStudio::Render {

class ScenePlanner;

// Tunables for one render pipeline instance; defaults mirror the operational
// contract (see AppConstants).
struct RenderPipelineConfig
{
    QVector<double> supportedGenerationDurations{4.0, 6.0, 8.0};
    double maxRetimeFactor = 1.10;
    double maxFreezeSeconds = 0.5;
    int normalizeFps = 24;
    int audioBitrateKbps = 192;
    QString language = QStringLiteral("vi");
    int videoPollMinMs = 3'000;
    int videoPollMaxMs = 5'000;
    qint64 videoTaskBudgetMs = 900'000;
    int maxSubmissionsPerScene = 2; // fresh submissions per scene per run
};

enum class RunOutcome
{
    Completed,
    WaitingForProvider, // provider tasks still outstanding — resume later
    Failed
};

QString runOutcomeToString(RunOutcome outcome);

// Drives one Render job through the state chart on the calling thread:
//
//   VALIDATING → TTS_RUNNING → TTS_READY → PLANNING → SCENES_READY
//   → VIDEO_RUNNING → CLIPS_READY → POST_PROCESSING → VERIFYING → COMPLETED
//
// Every stage transition is persisted through JobStore (validated against the
// P1 state machine) so a crash or restart resumes from the last durable step:
// finished stages are skipped based on artifacts on disk + persisted state.
// Video generation persists provider task ids into timeline/scenes.json after
// every transition ("never pay twice": an interrupted run reconciles instead
// of resubmitting).
//
// Blocking — invoke from a worker thread. Emits progress via queued signals.
class RenderPipeline : public QObject
{
    Q_OBJECT

public:
    RenderPipeline(Jobs::JobStore &store,
                   Providers::TtsClient &tts,
                   Providers::LlmClient &llm,
                   Providers::VideoGatewayClient &video,
                   const Media::Ffprobe &ffprobe,
                   const QString &ffmpegBin,
                   RenderPipelineConfig config = {},
                   std::function<void(qint64 ms)> sleepFn = {});

    // Blocking run; safe to call again after WaitingForProvider.
    RunOutcome runJob(const QString &jobId, QString *error);

    // Cooperative cancellation checked at stage boundaries and inside poll
    // loops; the job lands in CANCELLED.
    void requestCancel();

Q_SIGNALS:
    void stageChanged(const QString &jobId, const QString &stateName, const QString &message);
    void sceneProgress(const QString &jobId, int doneScenes, int totalScenes);

private:
    struct Paths
    {
        QString root;             // job dir
        QString scriptTxt;
        QString masterAudio;
        QString scenesJson;
        QString captionsVtt;
        QString concatMp4;
        QString finalMp4;
        static Paths forJob(const QString &jobDir);
        QString rawClip(int sceneIndex) const;
        QString normalizedClip(int sceneIndex) const;
    };

    // Verdict for driving one scene clip to completion.
    enum class ClipVerdict
    {
        Ready,            // raw clip downloaded and on disk
        Outstanding,      // provider task still in flight / budget exhausted
        PermanentFailure  // do not retry — the job must fail
    };

    bool transition(Jobs::JobRecord record, Jobs::State to, const QString &message,
                    QString *error);
    bool failJob(const QString &jobId, const QString &message, QString *error);
    bool cancelled(const QString &jobId, QString *error);
    void sleepChunked(qint64 totalMs);

    bool validateStage(const Jobs::JobRecord &record, const Paths &paths,
                       const QJsonObject &params, QString *error);
    bool ttsStage(const Jobs::JobRecord &record, const QJsonObject &params,
                  const Paths &paths, double *audioDurationOut, QString *error);
    bool planningStage(const Jobs::JobRecord &record, const QJsonObject &params,
                       const Paths &paths, double audioDuration, ScenePlan *planOut,
                       QString *error);
    bool loadPlan(const Paths &paths, ScenePlan *planOut, QString *error);
    bool persistPlan(const Paths &paths, const ScenePlan &plan, QString *error);

    // Returns false only on fatal failure (*outcome carries the verdict).
    bool videoStage(const Jobs::JobRecord &record, const Paths &paths, ScenePlan *plan,
                    RunOutcome *outcome, QString *error);
    void generatePromptForScene(const Scene &scene, const ScenePlan &plan,
                                QString *promptOut) const;
    ClipVerdict processScene(Scene *scene, const QString &prompt, const Paths &paths,
                             QString *error);

    bool postProcessingStage(const Paths &paths, ScenePlan *plan, double audioDuration,
                             QString *error);
    bool verifyingStage(const Paths &paths, double audioDuration, QString *error);

    void emitStage(const QString &jobId, Jobs::State state, const QString &message);

    Jobs::JobStore &m_store;
    Providers::TtsClient &m_tts;
    Providers::LlmClient &m_llm;
    Providers::VideoGatewayClient &m_video;
    const Media::Ffprobe &m_ffprobe;
    QString m_ffmpegBin;
    RenderPipelineConfig m_config;
    std::function<void(qint64 ms)> m_sleep;
    std::atomic_bool m_cancelRequested{false};
};

} // namespace TtvStudio::Render
