#pragma once

#include <QObject>
#include <QString>
#include <QtQml>

namespace TtvStudio::Core {

// Persisted provider/tool configuration (QSettings under the app identity).
// Resolution order everywhere: environment variable → stored setting →
// built-in default. This is the P5 replacement for hand-edited env vars;
// the Settings UI writes here.
class SettingsStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Provider endpoints
    Q_PROPERTY(QString llmBaseUrl READ llmBaseUrl WRITE setLlmBaseUrl NOTIFY llmBaseUrlChanged FINAL)
    Q_PROPERTY(QString llmApiKey READ llmApiKey WRITE setLlmApiKey NOTIFY llmApiKeyChanged FINAL)
    Q_PROPERTY(QString llmModel READ llmModel WRITE setLlmModel NOTIFY llmModelChanged FINAL)
    Q_PROPERTY(QString ttsBaseUrl READ ttsBaseUrl WRITE setTtsBaseUrl NOTIFY ttsBaseUrlChanged FINAL)
    Q_PROPERTY(QString videoGatewayBaseUrl READ videoGatewayBaseUrl WRITE setVideoGatewayBaseUrl NOTIFY videoGatewayBaseUrlChanged FINAL)
    Q_PROPERTY(QString videoGatewayApiKey READ videoGatewayApiKey WRITE setVideoGatewayApiKey NOTIFY videoGatewayApiKeyChanged FINAL)
    Q_PROPERTY(QString videoModel READ videoModel WRITE setVideoModel NOTIFY videoModelChanged FINAL)
    // Tool locations
    Q_PROPERTY(QString whisperBin READ whisperBin WRITE setWhisperBin NOTIFY whisperBinChanged FINAL)
    Q_PROPERTY(QString whisperModel READ whisperModel WRITE setWhisperModel NOTIFY whisperModelChanged FINAL)

public:
    explicit SettingsStore(QObject *parent = nullptr);

    static SettingsStore *create(QQmlEngine *engine, QJSEngine *);

    QString llmBaseUrl() const { return m_llmBaseUrl; }
    QString llmApiKey() const { return m_llmApiKey; }
    QString llmModel() const { return m_llmModel; }
    QString ttsBaseUrl() const { return m_ttsBaseUrl; }
    QString videoGatewayBaseUrl() const { return m_videoGatewayBaseUrl; }
    QString videoGatewayApiKey() const { return m_videoGatewayApiKey; }
    QString videoModel() const { return m_videoModel; }
    QString whisperBin() const { return m_whisperBin; }
    QString whisperModel() const { return m_whisperModel; }

    void setLlmBaseUrl(const QString &);
    void setLlmApiKey(const QString &);
    void setLlmModel(const QString &);
    void setTtsBaseUrl(const QString &);
    void setVideoGatewayBaseUrl(const QString &);
    void setVideoGatewayApiKey(const QString &);
    void setVideoModel(const QString &);
    void setWhisperBin(const QString &);
    void setWhisperModel(const QString &);

    // Non-QML accessor for C++ callers (RenderController composition).
    static QString storedValue(const QString &key);
    static void setValue(const QString &key, const QString &value);

signals:
    void llmBaseUrlChanged();
    void llmApiKeyChanged();
    void llmModelChanged();
    void ttsBaseUrlChanged();
    void videoGatewayBaseUrlChanged();
    void videoGatewayApiKeyChanged();
    void videoModelChanged();
    void whisperBinChanged();
    void whisperModelChanged();

private:
    void load();
    QString m_llmBaseUrl, m_llmApiKey, m_llmModel;
    QString m_ttsBaseUrl, m_videoGatewayBaseUrl, m_videoGatewayApiKey, m_videoModel;
    QString m_whisperBin, m_whisperModel;
};

} // namespace TtvStudio::Core
