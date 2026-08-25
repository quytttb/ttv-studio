#include "RenderController.h"

#include <QMetaObject>
#include <QThreadPool>

#include <memory>
#include <mutex>

#include "media/Ffprobe.h"
#include "core/SettingsStore.h"
#include "providers/LlmClient.h"
#include "providers/QNamTransport.h"
#include "providers/TtsClient.h"
#include "providers/VideoGatewayClient.h"
#include "media/WhisperStt.h"
#include "media/YtDlp.h"
#include "redub/RedubPipeline.h"
#include "render/RenderPipeline.h"
#include "utils/AppConstants.h"
#include "utils/Paths.h"

namespace TtvStudio::Core {

namespace {

Render::RenderPipelineConfig pipelineConfig()
{
    Render::RenderPipelineConfig config;
    // Persisted render-device selection (Settings page); env has no say here.
    config.videoBackend =
        SettingsStore::storedValue(QStringLiteral("render_backend"));
    if (config.videoBackend.isEmpty())
        config.videoBackend = QLatin1String(Defaults::kDefaultRenderBackend);
    return config;
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

QString RenderController::createRedubJob(const QString &source, const QString &language)
{
    if (source.trimmed().isEmpty()) {
        setLastError(tr("Source must not be empty"));
        return {};
    }
    const bool isUrl = source.startsWith(QStringLiteral("http://"))
                       || source.startsWith(QStringLiteral("https://"));
    const auto result = m_store.createJob(
        Jobs::Kind::Redub,
        QJsonObject{{QLatin1String(isUrl ? "source_url" : "source_path"), source},
                    {QLatin1String("language"), language}});
    if (!result.ok()) {
        setLastError(result.error);
        return {};
    }
    setLastError(QString());
    refreshJobs();
    return result.record->id;
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
    for (auto &fn : m_cancelHooks)
        fn();
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

        // Kind dispatch: Redub jobs drive RedubPipeline; others the render one.
        Media::YtDlp ytdlp;
        Media::WhisperStt whisper;
        Render::RenderPipeline renderPipeline{workerStore, tts,  llm,   video,
                                              ffprobe,     Paths::ffmpegBinary(), pipelineConfig()};
        Redub::RedubPipeline redubPipeline{workerStore, ytdlp, whisper, llm, tts,
                                           ffprobe,     Paths::ffmpegBinary()};
        Jobs::Kind kind = Jobs::Kind::Render;
        if (const auto record = workerStore.loadJob(jobId))
            kind = record->kind;

        connect(&renderPipeline, &Render::RenderPipeline::stageChanged, this,
                &RenderController::onStageChanged, Qt::QueuedConnection);
        connect(&renderPipeline, &Render::RenderPipeline::sceneProgress, this,
                &RenderController::onSceneProgress, Qt::QueuedConnection);
        connect(&redubPipeline, &Redub::RedubPipeline::stageChanged, this,
                &RenderController::onStageChanged, Qt::QueuedConnection);
        connect(&redubPipeline, &Redub::RedubPipeline::segmentProgress, this,
                &RenderController::onSceneProgress, Qt::QueuedConnection);

        {
            std::lock_guard lock(m_pipelineMutex);
            m_cancelHooks = {[&renderPipeline] { renderPipeline.requestCancel(); },
                             [&redubPipeline] { redubPipeline.requestCancel(); }};
        }

        QString error;
        bool ok = false;
        try {
            ok = kind == Jobs::Kind::Redub ? redubPipeline.runJob(jobId, &error)
                                                 == Redub::RunOutcome::Completed
                                           : renderPipeline.runJob(jobId, &error)
                                                 == Render::RunOutcome::Completed;
        } catch (const std::exception &e) {
            error = QStringLiteral("worker exception: %1").arg(e.what());
            qCritical() << "Render worker thread exception:" << e.what();
        } catch (...) {
            error = QStringLiteral("unknown worker exception");
        }

        {
            std::lock_guard lock(m_pipelineMutex);
            m_cancelHooks.clear();
        }

        // `ok` decided above
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
