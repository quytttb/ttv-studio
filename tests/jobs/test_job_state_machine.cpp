#include <QtTest>

#include "jobs/JobTypes.h"

using namespace TtvStudio::Jobs;

class TestJobStateMachine : public QObject
{
    Q_OBJECT

private slots:
    void mainlineOrdersMatchPipelineDocs()
    {
        const QVector<State> render = mainlineOrder(Kind::Render);
        QCOMPARE(render.size(), 11);
        QCOMPARE(render.first(), State::Created);
        QCOMPARE(render.at(2), State::TtsRunning);
        QCOMPARE(render.last(), State::Completed);

        const QVector<State> redub = mainlineOrder(Kind::Redub);
        QVERIFY(redub.size() > render.size());
        QCOMPARE(redub.at(2), State::Ingesting);
        // Redub rejoins the shared tail at TTS_RUNNING.
        const int join = redub.indexOf(State::TtsRunning);
        QVERIFY(join > 0);
        for (int i = join; i < redub.size(); ++i)
            QCOMPARE(redub.at(i), render.at(i - (join - 2)));
    }

    void happyPathWalksOneStepAtATime()
    {
        State state = State::Created;
        for (int step = 0; step < mainlineOrder(Kind::Redub).size() - 1; ++step) {
            const State next = mainlineSuccessor(Kind::Redub, state);
            QVERIFY(canTransition(Kind::Redub, state, next));
            state = next;
        }
        QCOMPARE(state, State::Completed);
        QVERIFY(isTerminal(state));
    }

    void rejectsSkippingAhead()
    {
        QVERIFY(!canTransition(Kind::Render, State::Created,
                               State::VideoRunning));
        QVERIFY(!canTransition(Kind::Redub, State::TranscriptReady,
                               State::Verifying));
    }

    void terminalStatesNeverLeave()
    {
        for (State terminal : {State::Completed, State::Failed,
                               State::Cancelled}) {
            QVERIFY(isTerminal(terminal));
            QVERIFY(!canTransition(Kind::Render, terminal, State::Created));
            // Only an identical-state save may target a terminal state.
            QVERIFY(!canTransition(Kind::Render, terminal,
                                   terminal == State::Failed
                                       ? State::Cancelled
                                       : State::Failed));
        }
    }

    void anyNonTerminalCanAbort()
    {
        for (State mid : {State::TtsRunning, State::Transcribing,
                          State::PostProcessing}) {
            QVERIFY(canTransition(Kind::Render, mid, State::Failed));
            QVERIFY(canTransition(Kind::Render, mid, State::Cancelled));
        }
    }

    void recoveryRequiresPendingStateToResume()
    {
        const std::optional<State> pending = State::TtsRunning;
        QVERIFY(!canTransition(Kind::Render, State::WaitingForProvider,
                               State::TtsRunning));          // no pending given
        QVERIFY(canTransition(Kind::Render, State::WaitingForProvider,
                              State::TtsRunning, pending));  // resume
        QVERIFY(!canTransition(Kind::Render, State::UnknownProviderState,
                               State::Planning, pending));   // wrong target
        QVERIFY(canTransition(Kind::Render, State::WaitingForProvider,
                              State::Cancelled));            // abort ok
    }

    void idempotentSaveIsAllowed()
    {
        QVERIFY(canTransition(Kind::Render, State::TtsRunning,
                              State::TtsRunning));
    }
};

QTEST_MAIN(TestJobStateMachine)
#include "test_job_state_machine.moc"
