// Phase 5 tests: TrainSessionAdapter Qt observer adapter.
//
// No sleep(), no event loop, no GUI window: the adapter is
// driven with an explicit tick() call, exactly as core tests
// drive TrainSession::tick(), and signals are asserted with
// QSignalSpy (direct connections, same thread, so no event
// loop is needed for delivery). The QTimer is for production
// only and is not started in any test here, except for the
// isTicking check, which starts and immediately stops it.

#include "Core/FakeDevice.h"
#include "Core/TrainSession.h"
#include "Train/TrainSessionAdapter.h"

#include <QSignalSpy>
#include <QTest>

namespace {

void drive(TrainSession &session, FakeDevice *fake, FakeClock &clock,
           int64_t duration_ms) {
    for (int64_t elapsed = 0; elapsed < duration_ms; elapsed += 250) {
        if (fake != nullptr) {
            fake->tick(clock.nowMonotonicMs());
        }
        session.tick();
        clock.advanceMs(250);
    }
}

Workout twoStepWorkout() {
    Workout workout;
    workout.name = "two-step";
    workout.steps = {
        {60, 250, 0, 0, "step1"},
        {60, 150, 0, 0, "step2"},
    };
    return workout;
}

} // namespace

class TestTrainSessionAdapter : public QObject {
    Q_OBJECT

private slots:
    void telemetrySignalFires() {
        FakeClock clock;
        TrainSession session;
        session.setClock(&clock);
        InMemoryRecorder recorder;
        session.setRecorder(&recorder);
        AthleteProfile profile;
        profile.ftp_watts = 250;
        session.setAthleteProfile(profile);
        FakeDeviceConfig config;
        config.name = "trainer";
        config.erg_tau_ms = 1000;
        auto fake_ptr = std::make_unique<FakeDevice>(config);
        FakeDevice *fake = fake_ptr.get();
        session.addDevice(std::move(fake_ptr));

        TrainSessionAdapter adapter(session);
        QSignalSpy telemetrySpy(&adapter,
                                &TrainSessionAdapter::telemetryUpdated);
        QSignalSpy stateSpy(&adapter,
                            &TrainSessionAdapter::sessionStateChanged);
        QSignalSpy stepSpy(&adapter,
                           &TrainSessionAdapter::workoutStepChanged);

        adapter.loadWorkout(twoStepWorkout());
        adapter.startSession();
        // Manual ticks through the adapter, not the session.
        for (int i = 0; i < 8; ++i) {
            fake->tick(clock.nowMonotonicMs());
            adapter.tick();
            clock.advanceMs(250);
        }
        QVERIFY(telemetrySpy.count() == 8);
        QVERIFY(stateSpy.count() >= 1);
        // The telemetry signal carries the merged sample.
        // The first sample is invalid (the trainer had not
        // been commanded yet on the first tick, as in the
        // headless harness), but later samples are valid
        // and converging toward the 250 W target.
        QList<QVariant> last = telemetrySpy.at(7);
        TelemetrySample sample =
            last.at(0).value<TelemetrySample>();
        QVERIFY(sample.power_valid);
        QVERIFY(sample.watts > 0);
        // Step signal fired for step 0.
        QVERIFY(stepSpy.count() >= 1);
        QCOMPARE(stepSpy.at(0).at(0).toInt(), 0);
    }

    void slotsForwardIntents() {
        FakeClock clock;
        TrainSession session;
        session.setClock(&clock);
        InMemoryRecorder recorder;
        session.setRecorder(&recorder);
        TrainSessionAdapter adapter(session);
        QCOMPARE(session.state(), SessionState::Idle);
        adapter.loadWorkout(twoStepWorkout());
        QCOMPARE(session.state(), SessionState::Ready);
        adapter.startSession();
        QCOMPARE(session.state(), SessionState::Running);
        adapter.pauseSession();
        QCOMPARE(session.state(), SessionState::Paused);
        adapter.resumeSession();
        QCOMPARE(session.state(), SessionState::Running);
        adapter.lapSession();
        // Lap while running is recorded (recorder is in-memory).
        QCOMPARE(recorder.laps().size(), size_t(1));
        adapter.stopAndSaveSession();
        QCOMPARE(session.state(), SessionState::Finished);
        QVERIFY(recorder.finished());
    }

    void manualCommandThroughAdapter() {
        FakeClock clock;
        TrainSession session;
        session.setClock(&clock);
        InMemoryRecorder recorder;
        session.setRecorder(&recorder);
        AthleteProfile profile;
        profile.ftp_watts = 250;
        session.setAthleteProfile(profile);
        FakeDeviceConfig config;
        config.name = "trainer";
        auto fake_ptr = std::make_unique<FakeDevice>(config);
        FakeDevice *fake = fake_ptr.get();
        session.addDevice(std::move(fake_ptr));
        TrainSessionAdapter adapter(session);
        adapter.loadWorkout(twoStepWorkout());
        adapter.startSession();
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastErgTarget(), 250.0);
        TrainerCommand manual;
        manual.mode = TrainerCommand::Mode::Erg;
        manual.target_watts = 100;
        adapter.setManualCommand(manual);
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastErgTarget(), 100.0);
    }

    void tickingFlag() {
        FakeClock clock;
        TrainSession session;
        session.setClock(&clock);
        TrainSessionAdapter adapter(session);
        QVERIFY(!adapter.isTicking());
        adapter.startTicking(250);
        QVERIFY(adapter.isTicking());
        adapter.stopTicking();
        QVERIFY(!adapter.isTicking());
        // Manual tick still works after stopping the timer.
        adapter.tick();
    }

    void destructionUnsubscribes() {
        FakeClock clock;
        TrainSession session;
        session.setClock(&clock);
        InMemoryRecorder recorder;
        session.setRecorder(&recorder);
        {
            TrainSessionAdapter adapter(session);
            adapter.startSession();
            adapter.tick();
        }
        // Adapter destroyed: session still ticks without
        // crashing, and no signal is emitted to a dead object.
        session.tick();
        QCOMPARE(session.state(), SessionState::Running);
    }
};

QTEST_MAIN(TestTrainSessionAdapter)
#include "testTrainSessionAdapter.moc"
