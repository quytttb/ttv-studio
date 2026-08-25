#include "RenderController.h"

#include <QMetaObject>
#include <QThreadPool>

#include <memory>
#include <mutex>

#include "media/Ffprobe.h"
#include "providers/LlmClient.h"
#include "providers/QNamTransport.h"
#include "providers/TtsClient.h"
#include "providers/VideoGatewayClient.h"
#include "render/RenderPipeline.h"
#include "utils/AppConstants.h"
#include "utils/Paths.h"

namespace TtvStudio::Core {

namespace {

Render::RenderPipelineConfig pipelineConfig()
{
    return Render::RenderPipelineConfig{};
}

} // namespace

RenderController::RenderController(QObject *parent)
    : QObject(parent),
      m_endpoints(ProviderEndpoints::fromEnvironment())
{
    initStore(Paths::jobsRoot());
}

RenderController::RenderController(Providers::ITransport &transport,
                                   const QString &storageRoot, QObject *parent)
    : QObject(parent),
      m_endpoints(ProviderEndpoints::fromEnvironment()),
      m_injectedTransport(&transport)
{
    initStore(storageRoot);
}

RenderController::~RenderController() = default;

void RenderController::initStore(const QString &storageRoot)
{
    m_jobs = new JobListModel(this);
    if (!m_store.setRoot(storageRoot))
        setLastError(QStringLiteral("cannot initialize job storage at %1").arg(storageRoot));
    refreshJobs();
}

void RenderController::setLastError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

void RenderController::refreshJobs()
{
    m_jobs->refresh(m_store.listJobs());
}

QString RenderController::createRenderJob(const QString &scriptText, const QString &language,
                                          const QString &aspectRatio, const QString &resolution)
{
    if (scriptText.trimmed().isEmpty()) {
        setLastError(tr("Script text must not be empty"));
        return {};
    }
    const auto result = m_store.createJob(
        Jobs::Kind::Render,
        QJsonObject{{QLatin1String("script_text"), scriptText},
                    {QLatin1String("language"), language},
                    {QLatin1String("aspect_ratio"), aspectRatio},
                    {QLatin1String("resolution"), resolution}});
    if (!result.ok()) {
        setLastError(result.error);
        return {};
    }
    setLastError(QString());
    refreshJobs();
    return result.record->id;
}

void RenderController::runJob(const QString &jobId)
{
    if (m_runActive) {
        setLastError(tr("A render run is already active"));
        return;
    }
    const auto record = m_store.loadJob(jobId);
    if (!record) {
        setLastError(tr("Unknown job: %1").arg(jobId));
        return;
    }
    if (Jobs::isTerminal(record->state)) {
        setLastError(tr("Job %1 already finished (%2)")
                         .arg(jobId, Jobs::stateToString(record->state)));
        return;
    }

    setLastError(QString());
    startRun(jobId);
}

void RenderController::cancelRun()
{
    std::lock_guard lock(m_pipelineMutex);
    if (m_activePipeline)
        m_activePipeline->requestCancel();
}

QString RenderController::finalVideoPath(const QString &jobId) const
{
    return m_store.jobDir(jobId) + QStringLiteral("/output/final_video.mp4");
}

void RenderController::startRun(const QString &jobId)
{
    m_runActive = true;
    m_activeJobId = jobId;
    m_activeStage.clear();
    m_scenesDone = 0;
    m_scenesTotal = 0;
    Q_EMIT runActiveChanged();
    Q_EMIT runStateChanged();

    // The HTTP stack must live on the thread that drives it
    // (QNetworkAccessManager affinity), so everything is built inside the
    // worker lambda and dies with it.
    const ProviderEndpoints endpoints = m_endpoints;
    const QString rootPath = m_store.rootPath();
    Providers::ITransport *injectedTransport = m_injectedTransport;

    QThreadPool::globalInstance()->start([this, jobId, endpoints, rootPath,
                                          injectedTransport]() {
        Jobs::JobStore workerStore;
        if (!workerStore.setRoot(rootPath)) {
            QMetaObject::invokeMethod(this, [this] { setLastError(tr("job storage failed")); },
                                      Qt::QueuedConnection);
            return;
        }

        Media::Ffprobe ffprobe;
        std::optional<Providers::QNamTransport> ownedTransport;
        if (!injectedTransport)
            ownedTransport.emplace();
        Providers::ITransport &transport =
            injectedTransport ? *injectedTransport : *ownedTransport;

        Providers::TtsClient tts{transport, ffprobe, endpoints.ttsBaseUrl};
        Providers::LlmConfig llmCfg;
        llmCfg.baseUrl = endpoints.llmBaseUrl;
        llmCfg.apiKey = endpoints.llmApiKey;
        llmCfg.model = endpoints.llmModel;
        Providers::LlmClient llm{transport, llmCfg};
        Providers::VideoGatewayClient video{transport, endpoints.videoGatewayBaseUrl,
                                            endpoints.videoGatewayApiKey,
                                            endpoints.videoModel};

        Render::RenderPipeline pipeline{workerStore, tts,  llm,   video,
                                        ffprobe,     Paths::ffmpegBinary(), pipelineConfig()};
        {
            std::lock_guard lock(m_pipelineMutex);
            // Non-owning handle so cancelRun() can reach it from the GUI thread.
            m_activePipeline = std::shared_ptr<Render::RenderPipeline>(&pipeline, [](auto *) {});
        }

        connect(&pipeline, &Render::RenderPipeline::stageChanged, this,
                &RenderController::onStageChanged, Qt::QueuedConnection);
        connect(&pipeline, &Render::RenderPipeline::sceneProgress, this,
                &RenderController::onSceneProgress, Qt::QueuedConnection);

        QString error;
        auto outcome = Render::RunOutcome::Failed;
        try {
            outcome = pipeline.runJob(jobId, &error);
        } catch (const std::exception &e) {
            error = QStringLiteral("worker exception: %1").arg(e.what());
            qCritical() << "Render worker thread exception:" << e.what();
        } catch (...) {
            error = QStringLiteral("unknown worker exception");
        }

        {
            std::lock_guard lock(m_pipelineMutex);
            m_activePipeline.reset();
        }

        const bool ok = outcome == Render::RunOutcome::Completed;
        const bool wasCancelled = error == QLatin1String("cancelled");
        QMetaObject::invokeMethod(
            this,
            [this, jobId, ok, wasCancelled, error] {
                m_runActive = false;
                m_activeStage.clear();
                refreshJobs();
                if (!ok && !wasCancelled)
                    setLastError(error);
                else if (wasCancelled)
                    setLastError(QString());
                Q_EMIT runActiveChanged();
                Q_EMIT runStateChanged();
                Q_EMIT jobFinished(jobId, ok);
            },
            Qt::QueuedConnection);
    });
}

void RenderController::onStageChanged(const QString &jobId, const QString &state,
                                      const QString &message)
{
    Q_UNUSED(message);
    if (jobId != m_activeJobId)
        return;
    m_activeStage = state;
    refreshJobs();
    Q_EMIT runStateChanged();
}

void RenderController::onSceneProgress(const QString &jobId, int done, int total)
{
    if (jobId != m_activeJobId)
        return;
    m_scenesDone = done;
    m_scenesTotal = total;
    Q_EMIT sceneProgressChanged();
}

} // namespace TtvStudio::Core
