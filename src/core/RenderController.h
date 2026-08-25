#pragma once

#include <functional>
#include <mutex>
#include <optional>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QtQml>

#include "core/JobListModel.h"
#include "core/ProviderEndpoints.h"
#include "jobs/JobStore.h"

namespace TtvStudio::Providers {
class ITransport;
}



namespace TtvStudio::Core {

// QML facade for the Render pipeline: job CRUD over JobStore, background
// execution of RenderPipeline on a worker thread, and progress properties
// for the UI. Transport is injectable for deterministic tests.
class RenderController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QAbstractListModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(bool runActive READ runActive NOTIFY runActiveChanged)
    Q_PROPERTY(QString activeJobId READ activeJobId NOTIFY runStateChanged)
    Q_PROPERTY(QString activeStage READ activeStage NOTIFY runStateChanged)
    Q_PROPERTY(int scenesDone READ scenesDone NOTIFY sceneProgressChanged)
    Q_PROPERTY(int scenesTotal READ scenesTotal NOTIFY sceneProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool llmConfigured READ llmConfigured CONSTANT)
    Q_PROPERTY(bool videoConfigured READ videoConfigured CONSTANT)

public:
    // Production constructor (QML): endpoints from environment.
    explicit RenderController(QObject *parent = nullptr);
    ~RenderController() override;

    // Test seam: drive the pipeline through an injected transport and a
    // caller-owned storage root.
    RenderController(Providers::ITransport &transport, const QString &storageRoot,
                     QObject *parent = nullptr);

    QAbstractListModel *jobs() const { return m_jobs; }
    bool runActive() const { return m_runActive; }
    QString activeJobId() const { return m_activeJobId; }
    QString activeStage() const { return m_activeStage; }
    int scenesDone() const { return m_scenesDone; }
    int scenesTotal() const { return m_scenesTotal; }
    QString lastError() const { return m_lastError; }
    bool llmConfigured() const { return m_endpoints.llmConfigured(); }
    bool videoConfigured() const { return m_endpoints.videoConfigured(); }

    // Returns the new job id (empty on failure).
    // Creates a Redub job; `source` is an http(s) URL or a local file path.
    Q_INVOKABLE QString createRedubJob(const QString &source,
                                       const QString &language = QStringLiteral("vi"));

    Q_INVOKABLE QString createRenderJob(const QString &scriptText,
                                     const QString &language = QStringLiteral("vi"),
                                     const QString &aspectRatio = QStringLiteral("16:9"),
                                     const QString &resolution = QStringLiteral("720p"));
    Q_INVOKABLE void runJob(const QString &jobId);
    Q_INVOKABLE void cancelRun();
    Q_INVOKABLE QString finalVideoPath(const QString &jobId) const;
    Q_INVOKABLE void refreshJobs();

signals:
    void runActiveChanged();
    void runStateChanged();
    void sceneProgressChanged();
    void lastErrorChanged();
    void jobFinished(const QString &jobId, bool ok);

private slots:
    void onStageChanged(const QString &jobId, const QString &state, const QString &message);
    void onSceneProgress(const QString &jobId, int done, int total);

private:
    void initStore(const QString &storageRoot);
    void setLastError(const QString &message);
    void startRun(const QString &jobId);

    Jobs::JobStore m_store;
    ProviderEndpoints m_endpoints;
    JobListModel *m_jobs = nullptr;

    bool m_runActive = false;
    QString m_activeJobId;
    QString m_activeStage;
    int m_scenesDone = 0;
    int m_scenesTotal = 0;
    QString m_lastError;

    // Test seam: drive HTTP through an injected transport instead of QNAM.
    Providers::ITransport *m_injectedTransport = nullptr;

    // Cancel hooks for the pipelines running on the worker thread; guarded
    // because cancelRun() touches them from the GUI thread.
    std::mutex m_pipelineMutex;
    QVector<std::function<void()>> m_cancelHooks;
};

} // namespace TtvStudio::Core
