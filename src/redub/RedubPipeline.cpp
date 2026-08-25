#include "RedubPipeline.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>

#include "media/MediaEngine.h"
#include "media/WhisperStt.h"
#include "redub/Translator.h"
#include "utils/AppConstants.h"

namespace TtvStudio::Redub {

namespace Jobs = TtvStudio::Jobs;

namespace {

bool writeFileAtomically(const QString &destination, const QByteArray &content, QString *error)
{
    QSaveFile part(destination);
    if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("cannot write %1").arg(destination);
        return false;
    }
    if (part.write(content) != content.size() || !part.commit()) {
        part.cancelWriting();
        *error = QStringLiteral("cannot publish %1").arg(destination);
        return false;
    }
    return true;
}

bool readFileIfExists(const QString &path, QByteArray *out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *out = file.readAll();
    return true;
}

} // namespace

RedubPipeline::Paths RedubPipeline::Paths::forJob(const QString &jobDir)
{
    Paths p;
    p.root = jobDir;
    p.sourceMp4 = jobDir + QStringLiteral("/input/source.mp4");
    p.sourceWav = jobDir + QStringLiteral("/work/source_audio.wav");
    p.transcriptJson = jobDir + QStringLiteral("/input/transcript.json");
    p.translationJson = jobDir + QStringLiteral("/input/translation.json");
    p.narrationDir = jobDir + QStringLiteral("/work/narration");
    p.fittedDir = jobDir + QStringLiteral("/work/fitted");
    p.dubTrackWav = jobDir + QStringLiteral("/work/dub_track.wav");
    p.finalMp4 = jobDir + QStringLiteral("/output/final_video.mp4");
    return p;
}

QString RedubPipeline::Paths::narration(int index) const
{
    return narrationDir + QStringLiteral("/%1.wav").arg(index, 3, 10, QChar('0'));
}

QString RedubPipeline::Paths::fitted(int index) const
{
    return fittedDir + QStringLiteral("/%1.wav").arg(index, 3, 10, QChar('0'));
}

RedubPipeline::RedubPipeline(Jobs::JobStore &store,
                             Media::YtDlp &ytdlp,
                             const Media::WhisperStt &whisper,
                             Providers::LlmClient &llm,
                             Providers::TtsClient &tts,
                             const Media::Ffprobe &ffprobe,
                             const QString &ffmpegBin,
                             RedubPipelineConfig config,
                             std::function<void(qint64 ms)> sleepFn)
    : m_store(store),
      m_ytdlp(ytdlp),
      m_whisper(whisper),
      m_llm(llm),
      m_tts(tts),
      m_ffprobe(ffprobe),
      m_ffmpegBin(ffmpegBin),
      m_config(std::move(config)),
      m_sleep(std::move(sleepFn))
{
    if (!m_sleep)
        m_sleep = [](qint64 ms) { QThread::msleep(quint64(ms)); };
}

void RedubPipeline::requestCancel()
{
    m_cancelRequested.store(true);
}

void RedubPipeline::emitStage(const QString &jobId, Jobs::State state, const QString &message)
{
    Q_EMIT stageChanged(jobId, Jobs::stateToString(state), message);
}

bool RedubPipeline::transition(Jobs::JobRecord record, Jobs::State to,
                               const QString &message, QString *error)
{
    record.state = to;
    const auto result = m_store.updateJob(record);
    if (!result.ok()) {
        *error = QStringLiteral("transition to %1 rejected: %2")
                     .arg(Jobs::stateToString(to), result.error);
        return false;
    }
    emitStage(result.record->id, to, message);
    return true;
}

bool RedubPipeline::failJob(const QString &jobId, const QString &message, QString *error)
{
    auto record = m_store.loadJob(jobId);
    if (record) {
        record->pendingState = std::nullopt;
        record->state = Jobs::State::Failed;
        (void)m_store.updateJob(*record);
    }
    *error = message;
    emitStage(jobId, Jobs::State::Failed, message);
    return false;
}

bool RedubPipeline::cancelled(const QString &jobId, QString *error)
{
    if (!m_cancelRequested.load())
        return false;
    auto record = m_store.loadJob(jobId);
    if (record && !Jobs::isTerminal(record->state)) {
        record->pendingState = std::nullopt;
        record->state = Jobs::State::Cancelled;
        (void)m_store.updateJob(*record);
        emitStage(jobId, Jobs::State::Cancelled, QStringLiteral("cancelled by user"));
    }
    *error = QStringLiteral("cancelled");
    return true;
}

bool RedubPipeline::ingestStage(const Jobs::JobRecord &record, const Paths &paths,
                                QString *error)
{
    const QString sourceUrl =
        record.params.value(QLatin1String("source_url")).toString().trimmed();
    const QString sourcePath =
        record.params.value(QLatin1String("source_path")).toString().trimmed();

    if (!sourcePath.isEmpty()) {
        // Local file: copy into the artifact store so the job is self-contained.
        if (!QFile::exists(sourcePath)) {
            *error = QStringLiteral("local source does not exist: %1").arg(sourcePath);
            return false;
        }
        QDir().mkpath(QFileInfo(paths.sourceMp4).absolutePath());
        QFile::remove(paths.sourceMp4);
        if (!QFile::copy(sourcePath, paths.sourceMp4)) {
            *error = QStringLiteral("cannot copy local source into the job");
            return false;
        }
    } else {
        Media::IngestError ingestError;
        if (!m_ytdlp.download(QUrl(sourceUrl), paths.sourceMp4, &ingestError)) {
            *error = ingestError.message;
            return false; // transient errors surface for retry at stage level
        }
    }

    // Fail-closed validation: must be a playable video with audio.
    const auto info = m_ffprobe.probe(paths.sourceMp4);
    if (!info) {
        *error = QStringLiteral("ffprobe failed on ingested source");
        return false;
    }
    if (!info->hasVideo || !info->hasAudio) {
        *error = QStringLiteral("ingested source lacks %1 stream")
                     .arg(info->hasVideo ? QStringLiteral("audio")
                                         : QStringLiteral("video"));
        return false;
    }
    return true;
}

bool RedubPipeline::transcribeStage(const Jobs::JobRecord &record, const Paths &paths,
                                    Transcript *out, QString *error)
{
    // Reuse an existing valid transcript on resume — transcription is expensive.
    QByteArray existing;
    if (readFileIfExists(paths.transcriptJson, &existing)) {
        Transcript stored;
        if (Transcript::fromJson(QJsonDocument::fromJson(existing).object(), &stored)) {
            *out = stored;
            return true;
        }
    }

    Media::MediaEngine engine{m_ffmpegBin, &m_ffprobe};
    QString engineError;
    if (!engine.extractAudio(paths.sourceMp4, paths.sourceWav, &engineError)) {
        *error = engineError;
        return false;
    }

    Transcript transcript;
    if (!m_whisper.transcribe(paths.sourceWav, record.id, &transcript, error))
        return false;

    QDir().mkpath(QFileInfo(paths.transcriptJson).absolutePath());
    if (!writeFileAtomically(paths.transcriptJson,
                             QJsonDocument(transcript.toJson()).toJson(),
                             error)) {
        return false;
    }
    *out = transcript;
    return true;
}

bool RedubPipeline::translateStage(const Jobs::JobRecord &, const Paths &paths,
                                   const Transcript &transcript, Translation *out,
                                   QString *error)
{
    // Resume: reuse translation when segment indices still match.
    QByteArray existing;
    if (readFileIfExists(paths.translationJson, &existing)) {
        Translation stored;
        if (Translation::fromJson(QJsonDocument::fromJson(existing).object(), &stored)
            && stored.segments.size() == transcript.segments.size()) {
            bool match = true;
            for (int i = 0; i < stored.segments.size() && match; ++i)
                match = stored.segments.at(i).index == transcript.segments.at(i).index;
            if (match) {
                *out = stored;
                return true;
            }
        }
    }

    TranslatorConfig config;
    config.targetLanguage =
        m_config.targetLanguage.isEmpty() ? QStringLiteral("vi") : m_config.targetLanguage;
    config.charsPerSecond = m_config.charsPerSecond;
    config.batchSize = m_config.translationBatchSize;
    TranscriptTranslator translator{m_llm, config};

    TranslationError translateError;
    if (!translator.translate(transcript, out, &translateError)) {
        *error = translateError.message;
        return false;
    }

    if (!writeFileAtomically(paths.translationJson,
                             QJsonDocument(out->toJson()).toJson(), error)) {
        return false;
    }
    return true;
}

bool RedubPipeline::ttsStage(const Jobs::JobRecord &record, const QJsonObject &params,
                             const Paths &paths, const Translation &translation,
                             QVector<double> *narrationDurations, QString *error)
{
    QDir().mkpath(paths.narrationDir);

    Providers::TtsRequest request;
    request.language =
        params.value(QLatin1String("language")).toString(m_config.targetLanguage);
    request.profileId = params.value(QLatin1String("tts_profile_id")).toString();

    const int total = translation.segments.size();
    for (int i = 0; i < total; ++i) {
        if (m_cancelRequested.load()) {
            *error = QStringLiteral("cancelled");
            return false;
        }
        const TranslatedSegment &segment = translation.segments.at(i);
        const QString dest = paths.narration(segment.index);

        // Resume-friendly: reuse a previously synthesized clip.
        const auto probeInfo = QFile::exists(dest) ? m_ffprobe.probe(dest) : std::nullopt;
        if (probeInfo && probeInfo->hasAudio && probeInfo->durationSec > 0.05) {
            narrationDurations->append(probeInfo->durationSec);
        } else {
            request.text = segment.text;
            const auto result = m_tts.synthesize(request, dest);
            if (!result.ok) {
                *error = QStringLiteral("segment %1 tts: %2")
                             .arg(segment.index)
                             .arg(result.error.message);
                return false;
            }
            narrationDurations->append(result.durationSec);
        }
        Q_EMIT segmentProgress(record.id, i + 1, total);
    }
    return true;
}

bool RedubPipeline::assemblyStage(const Jobs::JobRecord &record, const Paths &paths,
                                  const Transcript &transcript, QString *error)
{
    Media::MediaEngine engine{m_ffmpegBin, &m_ffprobe};
    QDir().mkpath(paths.fittedDir);

    QVector<double> windows;
    QVector<int> indexes;
    windows.reserve(transcript.segments.size());
    indexes.reserve(transcript.segments.size());
    for (const TranscriptSegment &s : std::as_const(transcript.segments)) {
        windows.append(s.durationSeconds());
        indexes.append(s.index);
    }

    QVector<double> narrationDurations;
    for (int index : indexes)
        narrationDurations.append(m_ffprobe.probe(paths.narration(index))
                                      ? m_ffprobe.probe(paths.narration(index))->durationSec
                                      : 0.0);

    DubPlanError planError;
    QVector<DubTiming> timings;
    if (!planDubTiming(windows, indexes, narrationDurations, m_config.minDubRate,
                       m_config.maxDubRate, &timings, &planError)) {
        *error = planError.message;
        return false;
    }

    QStringList fittedClips;
    QString engineError;
    int done = 0;
    for (const DubTiming &timing : std::as_const(timings)) {
        if (m_cancelRequested.load()) {
            *error = QStringLiteral("cancelled");
            return false;
        }
        const QString dest = paths.fitted(timing.segmentIndex);
        if (!QFile::exists(dest)) {
            if (!engine.fitNarration(paths.narration(timing.segmentIndex), dest,
                                     timing.atempoRate, timing.windowSeconds,
                                     &engineError)) {
                *error = QStringLiteral("segment %1: %2")
                             .arg(timing.segmentIndex)
                             .arg(engineError);
                return false;
            }
        }
        fittedClips.append(dest);
        Q_EMIT segmentProgress(record.id, ++done, timings.size());
    }

    return engine.concatAudio(fittedClips, paths.dubTrackWav, error);
}

RunOutcome RedubPipeline::runJob(const QString &jobId, QString *error)
{
    m_cancelRequested.store(false);

    const auto initial = m_store.loadJob(jobId);
    if (!initial) {
        *error = QStringLiteral("job not found: %1").arg(jobId);
        return RunOutcome::Failed;
    }
    if (initial->kind != Jobs::Kind::Redub) {
        *error = QStringLiteral("job %1 is not a redub job").arg(jobId);
        return RunOutcome::Failed;
    }
    if (Jobs::isTerminal(initial->state)) {
        *error = QStringLiteral("job %1 is terminal (%2)")
                     .arg(jobId, Jobs::stateToString(initial->state));
        return RunOutcome::Failed;
    }

    const Paths paths = Paths::forJob(m_store.jobDir(jobId));
    QDir().mkpath(QFileInfo(paths.sourceWav).absolutePath());
    QDir().mkpath(QFileInfo(paths.transcriptJson).absolutePath());

    auto record = *initial;

    // VALIDATING → INGESTING
    if (record.state == Jobs::State::Created) {
        const QString url = record.params.value(QLatin1String("source_url")).toString().trimmed();
        const QString path =
            record.params.value(QLatin1String("source_path")).toString().trimmed();
        if (url.isEmpty() && path.isEmpty()) {
            return failJob(jobId,
                           QStringLiteral("either source_url or source_path is required"),
                           error)
                       ? RunOutcome::Failed
                       : RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Validating, QStringLiteral("job accepted"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::Validating) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Ingesting, QStringLiteral("ingest started"),
                        error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }

    // INGESTING → SOURCE_READY
    if (record.state == Jobs::State::Ingesting) {
        if (!ingestStage(record, paths, error))
        {
            failJob(jobId, QStringLiteral("ingest: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::SourceReady,
                        QStringLiteral("source video ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }

    // SOURCE_READY → TRANSCRIBING → TRANSCRIPT_READY
    Transcript transcript;
    if (record.state == Jobs::State::SourceReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Transcribing,
                        QStringLiteral("transcription started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::Transcribing) {
        if (!transcribeStage(record, paths, &transcript, error))
        {
            failJob(jobId, QStringLiteral("transcribe: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::TranscriptReady,
                        QStringLiteral("transcript ready (%1 segments)")
                            .arg(transcript.segments.size()),
                        error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (transcript.segments.isEmpty()) {
        QByteArray blob;
        if (!readFileIfExists(paths.transcriptJson, &blob)) {
            failJob(jobId, QStringLiteral("transcript artifact missing"), error);
            return RunOutcome::Failed;
        }
        if (!Transcript::fromJson(QJsonDocument::fromJson(blob).object(), &transcript)) {
            failJob(jobId, QStringLiteral("transcript artifact malformed"), error);
            return RunOutcome::Failed;
        }
    }

    // TRANSCRIPT_READY → TRANSLATING → TRANSLATION_READY
    Translation translation;
    if (record.state == Jobs::State::TranscriptReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Translating,
                        QStringLiteral("translation started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::Translating) {
        if (!translateStage(record, paths, transcript, &translation, error))
        {
            failJob(jobId, QStringLiteral("translate: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::TranslationReady,
                        QStringLiteral("translation ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (translation.segments.isEmpty()) {
        QByteArray blob;
        if (!readFileIfExists(paths.translationJson, &blob)
            || !Translation::fromJson(QJsonDocument::fromJson(blob).object(), &translation)) {
            failJob(jobId, QStringLiteral("translation artifact missing or malformed"),
                    error);
            return RunOutcome::Failed;
        }
    }

    // TRANSLATION_READY → TTS_RUNNING → TTS_READY
    QVector<double> narrationDurations;
    if (record.state == Jobs::State::TranslationReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::TtsRunning,
                        QStringLiteral("narration synthesis started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::TtsRunning) {
        if (!ttsStage(record, record.params, paths, translation, &narrationDurations, error))
        {
            failJob(jobId, QStringLiteral("tts: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::TtsReady,
                        QStringLiteral("narration ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (narrationDurations.isEmpty()) {
        // Resumed run: re-probe existing narration clips.
        for (const TranscriptSegment &s : std::as_const(transcript.segments)) {
            const auto info = m_ffprobe.probe(paths.narration(s.index));
            if (!info || !info->hasAudio) {
                failJob(jobId,
                        QStringLiteral("narration clip for segment %1 missing").arg(s.index),
                        error);
                return RunOutcome::Failed;
            }
            narrationDurations.append(info->durationSec);
        }
    }

    // TTS_READY → PLANNING → SCENES_READY
    if (record.state == Jobs::State::TtsReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Planning,
                        QStringLiteral("dub timing planning"), error))
            return RunOutcome::Failed;
        if (!transition(m_store.loadJob(jobId).value_or(record), Jobs::State::ScenesReady,
                        QStringLiteral("dub timing ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }

    // SCENES_READY → VIDEO_RUNNING → CLIPS_READY (build dub track)
    if (record.state == Jobs::State::ScenesReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::VideoRunning,
                        QStringLiteral("dub track assembly started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::VideoRunning) {
        if (!assemblyStage(record, paths, transcript, error))
        {
            failJob(jobId, QStringLiteral("assembly: %1").arg(*error), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::ClipsReady,
                        QStringLiteral("dub track ready"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }

    // CLIPS_READY → POST_PROCESSING → VERIFYING → COMPLETED
    if (record.state == Jobs::State::ClipsReady) {
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::PostProcessing,
                        QStringLiteral("muxing dub onto source video"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::PostProcessing) {
        Media::MediaEngine engine{m_ffmpegBin, &m_ffprobe};
        const QString finalPart = paths.finalMp4 + QStringLiteral(".part");
        QString engineError;
        if (!engine.muxNarration(paths.sourceMp4, paths.dubTrackWav, finalPart,
                                 m_config.audioBitrateKbps, &engineError)) {
            QFile::remove(finalPart);
            failJob(jobId, QStringLiteral("post-processing: %1").arg(engineError),
                           error);
            return RunOutcome::Failed;
        }
        if (!QFile::rename(finalPart, paths.finalMp4)) {
            QFile::remove(finalPart);
            failJob(jobId, QStringLiteral("cannot publish final video"), error);
            return RunOutcome::Failed;
        }
        if (cancelled(jobId, error))
            return RunOutcome::Failed;
        if (!transition(record, Jobs::State::Verifying,
                        QStringLiteral("output verification started"), error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }
    if (record.state == Jobs::State::Verifying) {
        const auto info = m_ffprobe.probe(paths.finalMp4);
        if (!info || !info->hasVideo || !info->hasAudio) {
            failJob(jobId, QStringLiteral("final video failed verification"), error);
            return RunOutcome::Failed;
        }
        if (!transition(record, Jobs::State::Completed, QStringLiteral("redub completed"),
                        error))
            return RunOutcome::Failed;
        record = m_store.loadJob(jobId).value_or(record);
    }

    if (record.state != Jobs::State::Completed) {
        *error = QStringLiteral("unexpected terminal state: %1")
                     .arg(Jobs::stateToString(record.state));
        return RunOutcome::Failed;
    }
    return RunOutcome::Completed;
}

} // namespace TtvStudio::Redub
