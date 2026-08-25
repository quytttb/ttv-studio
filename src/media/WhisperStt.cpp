#include "WhisperStt.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "Subprocess.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Media {

namespace {

QString envOrEmpty(const char *name)
{
    return qEnvironmentVariable(name);
}

double hmsToSeconds(const QStringList &parts)
{
    if (parts.size() != 3)
        return -1.0;
    // whisper.cpp emits "HH:MM:SS.mmm" (some builds use a comma for ms).
    const QString secs = QString(parts.at(2)).replace(QChar(','), QChar('.'));
    return parts.at(0).toDouble() * 3600.0 + parts.at(1).toDouble() * 60.0
           + secs.toDouble();
}

// whisper.cpp -oj writes "<base>.json" with
// { "transcription": [ { "timestamps": {"from","to"}, "text": … } ] }
bool parseWhisperJson(const QByteArray &content, Redub::Transcript *out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(content);
    if (!doc.isObject())
        return false;
    const auto items = doc.object().value(QLatin1String("transcription")).toArray();
    if (items.isEmpty())
        return false;

    *out = Redub::Transcript{};
    int index = 0;
    for (const auto &v : items) {
        const QJsonObject item = v.toObject();
        const QJsonObject stamps = item.value(QLatin1String("timestamps")).toObject();
        const double start =
            hmsToSeconds(stamps.value(QLatin1String("from")).toString().split(QChar(':')));
        const double end =
            hmsToSeconds(stamps.value(QLatin1String("to")).toString().split(QChar(':')));
        const QString text = item.value(QLatin1String("text")).toString().trimmed();
        if (text.isEmpty() || end <= start)
            return false;

        Redub::TranscriptSegment segment;
        segment.index = ++index;
        segment.startSeconds = start;
        segment.endSeconds = end;
        segment.text = text;
        out->segments.append(segment);
    }
    return !out->segments.isEmpty();
}

} // namespace

WhisperStt::WhisperStt(Config config)
    : m_config(std::move(config))
{
}

// Resolution order: explicit config path → TTV_STUDIO_WHISPER_BIN →
// PATH ("whisper-cli" then legacy "main").
QString WhisperStt::resolveBinary(QString *error) const
{
    if (!m_config.binaryPath.isEmpty() && QFileInfo::exists(m_config.binaryPath))
        return m_config.binaryPath;

    const QString fromEnv = envOrEmpty("TTV_STUDIO_WHISPER_BIN");
    if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv))
        return fromEnv;

    for (const char *name : {"whisper-cli", "main"}) {
        const QString found = QStandardPaths::findExecutable(QLatin1String(name));
        if (!found.isEmpty())
            return found;
    }
    *error = QStringLiteral(
                 "whisper binary not found (install whisper.cpp or set "
                 "TTV_STUDIO_WHISPER_BIN)");
    return {};
}

bool WhisperStt::transcribe(const QString &wavPath, const QString &jobId,
                            Redub::Transcript *out, QString *error) const
{
    const QString binary = resolveBinary(error);
    if (binary.isEmpty())
        return false;

    QString model = m_config.modelPath;
    if (model.isEmpty())
        model = envOrEmpty("TTV_STUDIO_WHISPER_MODEL");
    if (model.isEmpty() || !QFileInfo::exists(model)) {
        *error = QStringLiteral(
                     "whisper model missing (set TTV_STUDIO_WHISPER_MODEL to a ggml .bin file)");
        return false;
    }

    // Output JSON lands next to the wav: <job>/work/transcript.json
    const QString outBase =
        QDir(QFileInfo(wavPath).absolutePath()).filePath(QStringLiteral("transcript"));

    QStringList args{QStringLiteral("-m"), model,
                     QStringLiteral("-f"), wavPath,
                     QStringLiteral("-oj"),
                     QStringLiteral("-of"), outBase};
    if (!m_config.language.isEmpty())
        args << QStringLiteral("-l") << m_config.language;

    Subprocess subprocess;
    const SubprocessResult result =
        subprocess.run(binary, args, Defaults::kPostProcessTimeoutMs);

    if (!result.started) {
        *error = QStringLiteral("whisper could not be started");
        return false;
    }
    if (result.timedOut) {
        *error = QStringLiteral("whisper timed out transcribing job %1").arg(jobId);
        return false;
    }
    if (!result.ok()) {
        *error = QStringLiteral("whisper failed (%1): %2")
                     .arg(result.exitCode)
                     .arg(result.stderrText.trimmed().section(QChar('\n'), -1));
        return false;
    }

    QFile jsonFile(outBase + QStringLiteral(".json"));
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("whisper did not produce %1.json")
                     .arg(QFileInfo(outBase).fileName());
        return false;
    }
    if (!parseWhisperJson(jsonFile.readAll(), out)) {
        *error = QStringLiteral("whisper output is malformed or empty");
        return false;
    }
    out->provider = QStringLiteral("whisper_local");
    return true;
}

} // namespace TtvStudio::Media
