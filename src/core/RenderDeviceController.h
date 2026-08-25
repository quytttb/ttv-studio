#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml>

namespace TtvStudio::Core {

// QML facade over Media::HardwareEncoder: discovers usable video encoder
// backends (CPU + probed hardware encoders), exposes them to the Settings UI
// and persists the selection through SettingsStore.
//
// Scan strategy (see HardwareEncoder): one throwaway lavfi encode per hardware
// candidate. Probes run one per event-loop tick so the UI stays responsive and
// progress can be shown; a full scan costs ~0.3s per candidate on real hw and
// fails fast when ffmpeg cannot start an encoder.
class RenderDeviceController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // A scan is in flight (backends/gpuAvailable not final yet).
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged FINAL)
    // At least one hardware backend passed its probe. Machines without a
    // capable GPU stay false — the settings section hides itself.
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY scanFinished FINAL)
    // [{id, label, hardware, usable}] — every candidate plus usability.
    Q_PROPERTY(QVariantList backends READ backends NOTIFY scanFinished FINAL)
    // Persisted selection ("cpu" by default; validated against the scan).
    Q_PROPERTY(QString selectedBackend READ selectedBackend WRITE setSelectedBackend
                   NOTIFY selectedBackendChanged FINAL)
    // Last "Test render" outcome for the selected backend.
    Q_PROPERTY(bool testRunning READ testRunning NOTIFY testRunningChanged FINAL)
    Q_PROPERTY(QString testMessage READ testMessage NOTIFY testResultChanged FINAL)
    Q_PROPERTY(qint64 lastTestMs READ lastTestMs NOTIFY testResultChanged FINAL)

public:
    explicit RenderDeviceController(QObject *parent = nullptr);

    static RenderDeviceController *create(QQmlEngine *engine, QJSEngine *);

    bool scanning() const { return m_scanning; }
    bool gpuAvailable() const { return m_gpuAvailable; }
    QVariantList backends() const { return m_backends; }
    QString selectedBackend() const;
    bool testRunning() const { return m_testRunning; }
    QString testMessage() const { return m_testMessage; }
    qint64 lastTestMs() const { return m_lastTestMs; }

    void setSelectedBackend(const QString &backend);

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void testSelected();

signals:
    void scanningChanged();
    void scanFinished();
    void selectedBackendChanged();
    void testRunningChanged();
    void testResultChanged();

private:
    void probeNext();
    QString resolveFfmpeg() const;

    bool m_scanning = false;
    bool m_gpuAvailable = false;
    bool m_scanScheduled = false; // guard against overlapping rescan requests
    QVariantList m_backends;
    QStringList m_probeQueue;

    bool m_testRunning = false;
    QString m_testMessage;
    qint64 m_lastTestMs = 0;
};

} // namespace TtvStudio::Core
