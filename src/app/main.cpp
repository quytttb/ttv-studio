#include "utils/AppConstants.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QStandardPaths>
#include <QString>
#include <QtDebug>

#include "ThemeSetup.h"

#ifdef Q_OS_WIN
#include "utils/os/WindowsFramelessHelper.h"
#endif

// ---------------------------------------------------------------------------
// File-based message handler (rotation via TtvStudio::Defaults).
// ---------------------------------------------------------------------------

namespace {

QFile  *g_logFile  = nullptr;
QMutex  g_logMutex;
QString g_logPath;

void fileMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = "DEBUG";
    switch (type) {
    case QtDebugMsg:    level = "DEBUG";    break;
    case QtInfoMsg:     level = "INFO";     break;
    case QtWarningMsg:  level = "WARNING";  break;
    case QtCriticalMsg: level = "CRITICAL"; break;
    case QtFatalMsg:    level = "FATAL";    break;
    }
    const QByteArray line =
        (QDateTime::currentDateTime().toString(Qt::ISODate)
         + QStringLiteral(" [") + QLatin1String(level) + QStringLiteral("] ")
         + msg + QLatin1Char('\n')).toUtf8();

    QMutexLocker lock(&g_logMutex);
    if (g_logFile && g_logFile->isOpen()) {
        if (g_logFile->size() + line.size() > TtvStudio::Defaults::kLogMaxBytes) {
            g_logFile->close();
            QFile::remove(g_logPath + QStringLiteral(".%1").arg(TtvStudio::Defaults::kLogKeepBackups));
            for (int i = TtvStudio::Defaults::kLogKeepBackups - 1; i >= 1; --i) {
                QFile::rename(g_logPath + QStringLiteral(".%1").arg(i),
                              g_logPath + QStringLiteral(".%1").arg(i + 1));
            }
            QFile::rename(g_logPath, g_logPath + QStringLiteral(".1"));
            if (g_logFile->open(QIODevice::Append | QIODevice::Text)) {
                g_logFile->write(line);
                g_logFile->flush();
            }
        } else {
            g_logFile->write(line);
            g_logFile->flush();
        }
    }
    fprintf(stderr, "%s", line.constData());
    if (type == QtFatalMsg)
        abort();
}

// Returns the path where the log file will be created, for display in the UI.
QString initFileLogging()
{
    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString logPath = dataDir + QStringLiteral("/ttv-studio.log");
    g_logPath = logPath;

    // Startup rotation for oversized leftovers.
    {
        QFileInfo fi(logPath);
        if (fi.exists() && fi.size() > TtvStudio::Defaults::kLogMaxBytes) {
            QFile::remove(logPath + QStringLiteral(".%1").arg(TtvStudio::Defaults::kLogKeepBackups));
            for (int i = TtvStudio::Defaults::kLogKeepBackups - 1; i >= 1; --i) {
                QFile::rename(logPath + QStringLiteral(".%1").arg(i),
                              logPath + QStringLiteral(".%1").arg(i + 1));
            }
            QFile::rename(logPath, logPath + QStringLiteral(".1"));
        }
    }

    g_logFile = new QFile(logPath);
    if (!g_logFile->open(QIODevice::Append | QIODevice::Text)) {
        delete g_logFile;
        g_logFile = nullptr;
        return {};
    }
    qInstallMessageHandler(fileMessageHandler);
    return logPath;
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Prefer Direct3D 11 as the default RHI backend on Windows.
    if (qgetenv("QSG_RHI_BACKEND").isEmpty()) {
        qputenv("QSG_RHI_BACKEND", "d3d11");
    }
#endif

    LoggerKit::Theme::applyQuickControlsStyle();

    QGuiApplication app(argc, argv);

#ifdef Q_OS_WIN
    static WindowsFramelessHelper framelessHelper;
    app.installNativeEventFilter(&framelessHelper);
#endif

    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/TtvStudio/Components/resources/icons/studio.svg")));
    QCoreApplication::setOrganizationName(QStringLiteral("quytttb"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.quytttb"));
    QCoreApplication::setApplicationName(QStringLiteral("TTV Studio"));

    const QString logFilePath = initFileLogging();
    qInfo() << "TTV Studio starting — log:" << logFilePath;

    // Font path matches qt_add_qml_module(RESOURCES) alias in the generated qrc.
    const QString iconFontPath =
        QStringLiteral(":/qt/qml/LoggerKit/Components/resources/fonts/MaterialSymbols/"
                       "MaterialSymbolsOutlined.ttf");
    const int fontId = QFontDatabase::addApplicationFont(iconFontPath);
    if (fontId < 0) {
        qWarning() << "[main] Failed to load icon font from" << iconFontPath
                   << "— icons will show as boxes";
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("TtvStudio.App", "Main");

    return app.exec();
}
