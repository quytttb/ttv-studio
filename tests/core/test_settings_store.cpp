#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "core/ProviderEndpoints.h"
#include "core/SettingsStore.h"

using namespace TtvStudio::Core;
using namespace TtvStudio;

class TestSettingsStore : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;

private slots:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // Hermetic storage: point the platform config location at the temp
        // dir and force the INI backend everywhere. The default QSettings
        // constructor on Windows uses the registry, which no env var can
        // redirect; INI resolves via QStandardPaths (XDG_CONFIG_HOME on
        // Unix, APPDATA on Windows).
        QCoreApplication::setOrganizationName(QStringLiteral("TtvStudioTestOrg"));
        QCoreApplication::setApplicationName(QStringLiteral("settings_store_test"));
        qputenv("XDG_CONFIG_HOME", m_tmp.path().toUtf8());
        qputenv("APPDATA", m_tmp.path().toUtf8());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    void persistsValues() {
        SettingsStore store;
        store.setLlmModel(QStringLiteral("gpt-x"));
        store.setVideoGatewayBaseUrl(QStringLiteral("http://10.0.0.5:8765"));
        QCOMPARE(store.llmModel(), QStringLiteral("gpt-x"));
        // Persisted keys are readable through the C++ accessor.
        QCOMPARE(SettingsStore::storedValue(QStringLiteral("llm_model")),
                 QStringLiteral("gpt-x"));
        QCOMPARE(SettingsStore::storedValue(QStringLiteral("video_gateway_base_url")),
                 QStringLiteral("http://10.0.0.5:8765"));
        // Endpoints merge prefers stored settings over defaults.
        auto endpoints = ProviderEndpoints::fromEnvironment();
        QCOMPARE(endpoints.llmModel, QStringLiteral("gpt-x"));
    }

    void renderBackendRoundTrip() {
        // Default is the CPU profile; a stored hardware backend survives a
        // fresh SettingsStore instance (QSettings re-read).
        QCOMPARE(SettingsStore::storedValue(QStringLiteral("render_backend")),
                 QString());
        SettingsStore store;
        QCOMPARE(store.renderBackend(), QStringLiteral("cpu"));

        store.setRenderBackend(QStringLiteral("h264_nvenc"));
        QCOMPARE(store.renderBackend(), QStringLiteral("h264_nvenc"));
        QCOMPARE(SettingsStore::storedValue(QStringLiteral("render_backend")),
                 QStringLiteral("h264_nvenc"));
        QCOMPARE(SettingsStore().renderBackend(), QStringLiteral("h264_nvenc"));

        // Empty writes fall back to the default instead of sticking.
        store.setRenderBackend(QStringLiteral(""));
        QCOMPARE(store.renderBackend(), QStringLiteral("cpu"));
    }
};

QTEST_GUILESS_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
