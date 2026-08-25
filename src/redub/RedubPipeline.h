#pragma once

#include <atomic>

#include <QObject>
#include <QString>

#include "jobs/JobStore.h"
#include "media/Ffprobe.h"
#include "media/YtDlp.h"
#include "providers/LlmClient.h"
#include "providers/TtsClient.h"
#include "redub/DubPlanner.h"
#include "redub/Transcript.h"

namespace TtvStudio::Media {
class WhisperStt;
}

namespace TtvStudio::Redub {

struct RedubPipelineConfig
{
    QString targetLanguage = QStringLiteral("vi");
    double charsPerSecond = 14.0;
    int translationBatchSize = 10;
    double minDubRate = 0.85;
    double maxDubRate = 1.25;
    int audioBitrateKbps = 192;
};

enum class RunOutcome
{
    Completed,
    Failed
};

// Drives one Redub job (existing video → new-language dub) through the state
// chart, mirroring RenderPipeline's durability model: every transition is
// persisted via JobStore; finished stages are skipped on restart based on
// artifacts on disk.
//
//   VALIDATING → INGESTING → SOURCE_READY → TRANSCRIBING → TRANSCRIPT_READY
//   → TRANSLATING → TRANSLATION_READY → TTS_RUNNING → TTS_READY → PLANNING
//   → SCENES_READY → VIDEO_RUNNING → CLIPS_READY → POST_PROCESSING
//   → VERIFYING → COMPLETED
//
// Blocking — invoke from a worker thread.
class RedubPipeline : public QObject
{
    Q_OBJECT

public:
    RedubPipeline(Jobs::JobStore &store,
                  Media::YtDlp &ytdlp,
                  const Media::WhisperStt &whisper,
                  Providers::LlmClient &llm,
                  Providers::TtsClient &tts,
                  const Media::Ffprobe &ffprobe,
                  const QString &ffmpegBin,
                  RedubPipelineConfig config = {},
                  std::function<void(qint64 ms)> sleepFn = {});

    RunOutcome runJob(const QString &jobId, QString *error);
    void requestCancel();

Q_SIGNALS:
    void stageChanged(const QString &jobId, const QString &stateName, const QString &message);
    void segmentProgress(const QString &jobId, int done, int total);

private:
    struct Paths
    {
        QString root;
        QString sourceMp4;
        QString sourceWav;
        QString transcriptJson;
        QString translationJson;
        QString narrationDir;
        QString fittedDir;
        QString dubTrackWav;
        QString finalMp4;

        static Paths forJob(const QString &jobDir);
        QString narration(int index) const;
        QString fitted(int index) const;
    };

    bool transition(Jobs::JobRecord record, Jobs::State to, const QString &message,
                    QString *error);
    bool failJob(const QString &jobId, const QString &message, QString *error);
    bool cancelled(const QString &jobId, QString *error);

    bool ingestStage(const Jobs::JobRecord &record, const Paths &paths, QString *error);
    bool transcribeStage(const Jobs::JobRecord &record, const Paths &paths,
                         Transcript *out, QString *error);
    bool translateStage(const Jobs::JobRecord &record, const Paths &paths,
                        const Transcript &transcript, Translation *out, QString *error);
    bool ttsStage(const Jobs::JobRecord &record, const QJsonObject &params,
                  const Paths &paths, const Translation &translation,
                  QVector<double> *narrationDurations, QString *error);
    bool assemblyStage(const Jobs::JobRecord &record, const Paths &paths,
                       const Transcript &transcript, QString *error);

    void emitStage(const QString &jobId, Jobs::State state, const QString &message);

    Jobs::JobStore &m_store;
    Media::YtDlp &m_ytdlp;
    const Media::WhisperStt &m_whisper;
    Providers::LlmClient &m_llm;
    Providers::TtsClient &m_tts;
    const Media::Ffprobe &m_ffprobe;
    QString m_ffmpegBin;
    RedubPipelineConfig m_config;
    std::function<void(qint64 ms)> m_sleep;
    std::atomic_bool m_cancelRequested{false};
};

} // namespace TtvStudio::Redub
