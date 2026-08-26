#include "RenderDeviceController.h"

#include <QTimer>

#include "media/HardwareEncoder.h"
#include "core/SettingsStore.h"
#include "utils/AppConstants.h"
#include "utils/Paths.h"

namespace TtvStudio::Core {

RenderDeviceController::RenderDeviceController(QObject *parent)
    : QObject(parent)
{
}

RenderDeviceController *RenderDeviceController::create(QQmlEngine *engine,
                                                       QJSEngine *)
{
    return new RenderDeviceController(engine);
}

QString RenderDeviceController::resolveFfmpeg() const
{
    const QString ffmpegBinDir = SettingsStore::resolvedValue(
        "TTV_STUDIO_FFMPEG_BIN_DIR", QStringLiteral("ffmpeg_bin_dir"));
    return Paths::ffmpegBinary(ffmpegBinDir);
}

QString RenderDeviceController::selectedBackend() const
{
    const QString stored = SettingsStore::storedValue(QStringLiteral("render_backend"));
    if (stored.isEmpty())
        return QLatin1String(Defaults::kDefaultRenderBackend);
    return stored;
}

void RenderDeviceController::setSelectedBackend(const QString &backend)
{
    if (selectedBackend() == backend)
        return;
    SettingsStore::setValue(QStringLiteral("render_backend"), backend);
    Q_EMIT selectedBackendChanged();
}

void RenderDeviceController::rescan()
{
    if (m_scanning || m_scanScheduled)
        return;

    const QString ffmpeg = resolveFfmpeg();
    if (ffmpeg.isEmpty()) {
        m_gpuAvailable = false;
        m_backends.clear();
        Q_EMIT scanFinished();
        return;
    }

    m_backends.clear();
    m_probeQueue.clear();
    for (const Media::EncoderInfo &candidate : Media::HardwareEncoder::candidates())
        m_probeQueue.append(candidate.id);

    m_scanScheduled = true; // start on a clean stack so bindings settle first
    QTimer::singleShot(0, this, [this] {
        m_scanScheduled = false;
        if (m_scanning || m_probeQueue.isEmpty())
            return;
        m_scanning = true;
        Q_EMIT scanningChanged();
        probeNext();
    });
}

void RenderDeviceController::probeNext()
{
    if (!m_scanning) // cancelled / reentered — drop the queue
        return;

    const QString ffmpeg = resolveFfmpeg();

    // CPU is always usable when ffmpeg exists; hardware candidates must pass
    // a real probe encode. One probe per event-loop tick keeps the UI alive.
    if (!m_probeQueue.isEmpty()) {
        const QString id = m_probeQueue.takeFirst();
        const bool hardware = Media::HardwareEncoder::isHardware(id);
        const bool usable =
            !hardware || (!ffmpeg.isEmpty()
                          && Media::HardwareEncoder::probe(ffmpeg, id).ok);

        QVariantMap entry;
        for (const Media::EncoderInfo &candidate : Media::HardwareEncoder::candidates()) {
            if (candidate.id != id)
                continue;
            entry.insert(QStringLiteral("id"), candidate.id);
            entry.insert(QStringLiteral("label"), candidate.label);
            entry.insert(QStringLiteral("hardware"), candidate.hardware);
            break;
        }
        entry.insert(QStringLiteral("usable"), usable);
        m_backends.append(entry);

        if (hardware && usable)
            m_gpuAvailable = true;

        if (!m_probeQueue.isEmpty()) {
            QTimer::singleShot(0, this, &RenderDeviceController::probeNext);
            return;
        }
    }

    m_scanning = false;
    Q_EMIT scanningChanged();
    Q_EMIT scanFinished();
}

void RenderDeviceController::testSelected()
{
    if (m_testRunning)
        return;

    const QString backend = selectedBackend();
    const QString ffmpeg = resolveFfmpeg();
    m_testRunning = true;
    m_testMessage.clear();
    Q_EMIT testRunningChanged();

    // Let the running state reach the screen before the blocking probe.
    QTimer::singleShot(0, this, [this, backend, ffmpeg] {
        Media::EncoderProbeResult result;
        if (ffmpeg.isEmpty())
            result.error = QStringLiteral("ffmpeg not found");
        else
            result = Media::HardwareEncoder::probe(ffmpeg, backend);

        m_testRunning = false;
        m_lastTestMs = result.elapsedMs;
        m_testMessage = result.ok
                            ? QStringLiteral("OK — %1 ms").arg(result.elapsedMs)
                            : QStringLiteral("FAILED: %1").arg(result.error);
        Q_EMIT testRunningChanged();
        Q_EMIT testResultChanged();
    });
}

} // namespace TtvStudio::Core
