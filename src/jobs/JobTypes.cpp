#include "jobs/JobTypes.h"

#include <QHash>

namespace TtvStudio::Jobs {

namespace {

const QHash<Kind, QVector<State>> &orderTable()
{
    static const QHash<Kind, QVector<State>> table = [] {
        const QVector<State> sharedTail{
            State::TtsRunning,
            State::TtsReady,
            State::Planning,
            State::ScenesReady,
            State::VideoRunning,
            State::ClipsReady,
            State::PostProcessing,
            State::Verifying,
            State::Completed,
        };

        QVector<State> render{
            State::Created,
            State::Validating,
        };
        render += sharedTail;

        QVector<State> redub{
            State::Created,
            State::Validating,
            State::Ingesting,
            State::SourceReady,
            State::Transcribing,
            State::TranscriptReady,
            State::Translating,
            State::TranslationReady,
        };
        redub += sharedTail;

        QHash<Kind, QVector<State>> t;
        t.insert(Kind::Render, render);
        t.insert(Kind::Redub, redub);
        return t;
    }();
    return table;
}

} // namespace

QString kindToString(Kind kind)
{
    switch (kind) {
    case Kind::Render: return QStringLiteral("render");
    case Kind::Redub:  return QStringLiteral("redub");
    }
    return QStringLiteral("render");
}

std::optional<Kind> kindFromString(const QString &text)
{
    if (text == QLatin1String("render"))
        return Kind::Render;
    if (text == QLatin1String("redub"))
        return Kind::Redub;
    return std::nullopt;
}

QString stateToString(State state)
{
    switch (state) {
    case State::Created:              return QStringLiteral("CREATED");
    case State::Validating:           return QStringLiteral("VALIDATING");
    case State::Ingesting:            return QStringLiteral("INGESTING");
    case State::SourceReady:          return QStringLiteral("SOURCE_READY");
    case State::Transcribing:         return QStringLiteral("TRANSCRIBING");
    case State::TranscriptReady:      return QStringLiteral("TRANSCRIPT_READY");
    case State::Translating:          return QStringLiteral("TRANSLATING");
    case State::TranslationReady:     return QStringLiteral("TRANSLATION_READY");
    case State::TtsRunning:           return QStringLiteral("TTS_RUNNING");
    case State::TtsReady:             return QStringLiteral("TTS_READY");
    case State::Planning:             return QStringLiteral("PLANNING");
    case State::ScenesReady:          return QStringLiteral("SCENES_READY");
    case State::VideoRunning:         return QStringLiteral("VIDEO_RUNNING");
    case State::ClipsReady:           return QStringLiteral("CLIPS_READY");
    case State::PostProcessing:       return QStringLiteral("POST_PROCESSING");
    case State::Verifying:            return QStringLiteral("VERIFYING");
    case State::Completed:            return QStringLiteral("COMPLETED");
    case State::Failed:               return QStringLiteral("FAILED");
    case State::Cancelled:            return QStringLiteral("CANCELLED");
    case State::WaitingForProvider:   return QStringLiteral("WAITING_FOR_PROVIDER");
    case State::UnknownProviderState: return QStringLiteral("UNKNOWN_PROVIDER_STATE");
    }
    return QStringLiteral("CREATED");
}

std::optional<State> stateFromString(const QString &text)
{
    static const QHash<QString, State> lookup = [] {
        QHash<QString, State> l;
        for (State s : {State::Created, State::Validating, State::Ingesting,
                        State::SourceReady, State::Transcribing,
                        State::TranscriptReady, State::Translating,
                        State::TranslationReady, State::TtsRunning,
                        State::TtsReady, State::Planning, State::ScenesReady,
                        State::VideoRunning, State::ClipsReady,
                        State::PostProcessing, State::Verifying,
                        State::Completed, State::Failed, State::Cancelled,
                        State::WaitingForProvider,
                        State::UnknownProviderState}) {
            l.insert(stateToString(s), s);
        }
        return l;
    }();
    const auto it = lookup.constFind(text);
    if (it == lookup.cend())
        return std::nullopt;
    return it.value();
}

const QVector<State> &mainlineOrder(Kind kind)
{
    // constFind: must not use QHash::value(), which returns a temporary and
    // would make this reference dangle.
    return orderTable().constFind(kind).value();
}

bool isTerminal(State state)
{
    switch (state) {
    case State::Completed:
    case State::Failed:
    case State::Cancelled:
        return true;
    default:
        return false;
    }
}

bool isRecovery(State state)
{
    return state == State::WaitingForProvider
        || state == State::UnknownProviderState;
}

State mainlineSuccessor(Kind kind, State state)
{
    const QVector<State> &order = mainlineOrder(kind);
    const int index = order.indexOf(state);
    if (index < 0 || index + 1 >= order.size())
        return state; // not on the mainline, or already terminal
    return order.at(index + 1);
}

bool canTransition(Kind kind, State from, State to,
                   std::optional<State> pendingState)
{
    if (from == to)
        return true; // idempotent save (metadata refresh)

    if (isTerminal(from))
        return false; // terminal states never leave

    if (to == State::Failed || to == State::Cancelled)
        return true; // abort from anywhere non-terminal

    if (isRecovery(to))
        return true;

    if (isRecovery(from))
        return pendingState.has_value() && to == *pendingState;

    return to == mainlineSuccessor(kind, from);
}

} // namespace TtvStudio::Jobs
