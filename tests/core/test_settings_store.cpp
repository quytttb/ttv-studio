#include <QTemporaryDir>
#include <QtTest>
#include "core/SettingsStore.h"
#include "core/ProviderEndpoints.h"
using namespace TtvStudio::Core;
using namespace TtvStudio;
class TestSettingsStore : public QObject {
    Q_OBJECT
private slots:
    void persistsValues() {
        qputenv("XDG_CONFIG_HOME", "/tmp/opencode/settings_test");
        QDir("/tmp/opencode/settings_test").removeRecursively();
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
};
QTEST_GUILESS_MAIN(TestSettingsStore)
#include "test_settings_store.moc"
