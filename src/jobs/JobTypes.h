#pragma once

#include <optional>

#include <QVector>
#include <QString>

class QJsonObject;

namespace TtvStudio::Jobs {

// Two pipeline kinds share one state chart (docs REDUB-PIPELINE §2):
//   render: CREATED → VALIDATING → TTS_RUNNING → TTS_READY → PLANNING
//           → SCENES_READY → VIDEO_RUNNING → CLIPS_READY → POST_PROCESSING
//           → VERIFYING → COMPLETED
//   redub : CREATED → VALIDATING → INGESTING → SOURCE_READY → TRANSCRIBING
//           → TRANSCRIPT_READY → TRANSLATING → TRANSLATION_READY
//           → (shared tail from TTS_RUNNING)
// Recovery / terminal states are reachable from every non-terminal state.
enum class Kind { Render, Redub };

enum class State {
    Created,
    // shared head
    Validating,
    // redub-only branch
    Ingesting,
    SourceReady,
    Transcribing,
    TranscriptReady,
    Translating,
    TranslationReady,
    // shared tail
    TtsRunning,
    TtsReady,
    Planning,
    ScenesReady,
    VideoRunning,
    ClipsReady,
    PostProcessing,
    Verifying,
    Completed,
    // terminal
    Failed,
    Cancelled,
    // recovery
    WaitingForProvider,
    UnknownProviderState,
};

QString kindToString(Kind kind);
std::optional<Kind> kindFromString(const QString &text);

QString stateToString(State state);
std::optional<State> stateFromString(const QString &text);

const QVector<State> &mainlineOrder(Kind kind);
bool isTerminal(State state);
bool isRecovery(State state);

// Next mainline state after `state`; returns `state` unchanged when it is not
// on the mainline or already terminal.
State mainlineSuccessor(Kind kind, State state);

// Transition rule:
//   - identical state  → allowed (idempotent metadata saves),
//   - terminal source  → never leaves,
//   - FAILED/CANCELLED target → always allowed while non-terminal,
//   - recovery target  → allowed; the caller must record `pendingState`,
//   - leaving a recovery state → only to the recorded `pendingState`
//     (`pendingState` must be provided) or to an abort terminal,
//   - otherwise        → exactly the next mainline step.
bool canTransition(Kind kind, State from, State to,
                   std::optional<State> pendingState = {});

} // namespace TtvStudio::Jobs
