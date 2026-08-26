#pragma once

#include <QObject>
#include <QString>
#include <QtQml>

#include <QTimer>

namespace TtvStudio::Core {

// In-app update lifecycle over the GitHub Releases API:
//
//   idle → checking → uptodate | available
//   available → downloading → downloaded
//   downloaded → installing → idle ("installed" message)   [Linux pkexec/apt]
//                      └→ app quits into the installer     [Windows]
//   any state → error (message set; state returns to the last stable one)
//
// Network operations run on the global thread pool; results marshal back via
// queued invokes so the UI never blocks. Download progress is derived by
// sampling the growing .part file against the asset's advertised size.
class UpdateController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Build-time version ("major.minor.patch"), from generated Version.h.
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    // idle | checking | uptodate | available | downloading | downloaded |
    // installing | installed | error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY releaseChanged FINAL)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseChanged FINAL)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY releaseChanged FINAL)
    // 0..1 when the asset size is known; -1 → indeterminate.
    Q_PROPERTY(qreal downloadProgress READ downloadProgress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged FINAL)

public:
    explicit UpdateController(QObject *parent = nullptr);

    static UpdateController *create(QQmlEngine *engine, QJSEngine *);

    QString currentVersion() const { return m_currentVersion; }
    QString state() const { return m_state; }
    bool busy() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QUrl releaseUrl() const { return m_releaseUrl; }
    qreal downloadProgress() const { return m_downloadProgress; }
    QString errorMessage() const { return m_errorMessage; }

    // Startup hook — called from Main.qml so tests constructing the controller
    // directly never touch the network.
    Q_INVOKABLE void scheduleStartupCheck();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void installUpdate();
    Q_INVOKABLE void dismissError();

signals:
    void stateChanged();
    void releaseChanged();
    void progressChanged();
    void errorChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void resetRelease();
    QString downloadDir() const;

    QString m_currentVersion;
    QString m_state = QStringLiteral("idle");
    QString m_latestVersion;
    QString m_releaseNotes;
    QUrl m_releaseUrl;
    qreal m_downloadProgress = -1.0;
    QString m_errorMessage;

    QString m_pendingAssetName;
    qint64 m_pendingAssetSize = 0;
    QTimer m_progressTimer;
};

} // namespace TtvStudio::Core
