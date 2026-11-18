// Phase 4 tests: TrainSession, LiveMetrics, Recorder, and the
// full-workout integration test (TDD 15.2).
//
// No sleep() anywhere: all time is a FakeClock, all devices
// are manually ticked (TDD 15.4 — manual ticking is the
// feature). Tests keep a raw FakeDevice* before moving it
// into the session, as TDD 15.4 suggests, and tick it
// themselves before each session tick.

#include "Core/FakeDevice.h"
#include "Core/LiveMetrics.h"
#include "Core/Recorder.h"
#include "Core/TrainSession.h"

#include <QTest>
#include <vector>

namespace {

class RecordingObserver : public ISessionObserver {
public:
    std::vector<TelemetrySample> telemetry;
    std::vector<SessionState> states;
    std::vector<int> steps;
    void onTelemetry(const TelemetrySample &sample) override {
        telemetry.push_back(sample);
    }
    void onSessionStateChanged(SessionState state) override {
        states.push_back(state);
    }
    void onWorkoutStepChanged(int index) override {
        steps.push_back(index);
    }
};

// Drive a session for duration_ms at 250 ms ticks, ticking
// the fake device first, then the session, then the clock.
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

TrainSession makeSession(FakeClock &clock, InMemoryRecorder &recorder,
                         FakeDevice *&fake_out) {
    TrainSession session;
    session.setClock(&clock);
    session.setRecorder(&recorder);
    AthleteProfile profile;
    profile.ftp_watts = 250;
    profile.weight_kg = 75;
    session.setAthleteProfile(profile);
    FakeDeviceConfig config;
    config.name = "trainer";
    config.erg_tau_ms = 1000;
    auto fake_ptr = std::make_unique<FakeDevice>(config);
    fake_out = fake_ptr.get();
    session.addDevice(std::move(fake_ptr));
    return session;
}

} // namespace

class TestTrainSession : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------ lifecycle
    void lifecycleStates() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        RecordingObserver observer;
        session.subscribe(&observer);
        QCOMPARE(session.state(), SessionState::Idle);
        session.loadWorkout(twoStepWorkout());
        QCOMPARE(session.state(), SessionState::Ready);
        session.start();
        QCOMPARE(session.state(), SessionState::Running);
        QVERIFY(recorder.started());
        QVERIFY(!recorder.finished());
        session.pause();
        QCOMPARE(session.state(), SessionState::Paused);
        session.resume();
        QCOMPARE(session.state(), SessionState::Running);
        session.stopAndSave();
        QCOMPARE(session.state(), SessionState::Finished);
        QVERIFY(recorder.finished());
        // Observer saw the state transitions in order.
        QVERIFY(observer.states.size() >= 4);
        QCOMPARE(observer.states.front(), SessionState::Ready);
        QCOMPARE(observer.states.back(), SessionState::Finished);
    }

    void workoutCommandReachesTrainer() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 5000);
        // The trainer received an ERG command for step 1 (250 W).
        QVERIFY(fake->hasErgTarget());
        QCOMPARE(fake->lastErgTarget(), 250.0);
        // And the recorded sample carries the target.
        QVERIFY(!recorder.samples().empty());
        QCOMPARE(recorder.samples().back().target_watts, 250.0);
    }

    void manualOverrideBeatsWorkout() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastErgTarget(), 250.0);
        // Manual ERG command while the workout is running.
        TrainerCommand manual;
        manual.mode = TrainerCommand::Mode::Erg;
        manual.target_watts = 100;
        session.setManualCommand(manual);
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastErgTarget(), 100.0);
        // Clearing the manual command returns to the workout.
        TrainerCommand idle;
        session.setManualCommand(idle);
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastErgTarget(), 250.0);
    }

    void simGradientWithoutWorkout() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.start(); // no workout loaded: free ride
        session.setSimGradient(5);
        drive(session, fake, clock, 2000);
        QCOMPARE(fake->lastSimGradient(), 5.0);
    }

    void simGradientZeroIsLiveNotOff() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.start(); // no workout loaded: free ride
        session.setSimGradient(0); // flat course: still a gradient
        // Past the 3 s stale timeout, so a stalled device
        // would show up as an invalid merged sample.
        drive(session, fake, clock, 5000);
        // The trainer still receives the Sim command at 0% —
        // the fake only emits while a command is active — and
        // the merged sample stays valid instead of going stale.
        // A flat must not freeze the ride.
        QVERIFY(fake->emittedCount() > 0);
        QCOMPARE(fake->lastSimGradient(), 0.0);
        QVERIFY(session.latest().power_valid);
    }

    void pauseStopsRecordingAndCommands() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 2000);
        size_t recorded_before = recorder.samples().size();
        double target_before = fake->lastErgTarget();
        session.pause();
        drive(session, fake, clock, 5000);
        // No new recorded samples while paused, and the trainer
        // command did not change (paused ticks do not command).
        QCOMPARE(recorder.samples().size(), recorded_before);
        QCOMPARE(fake->lastErgTarget(), target_before);
        session.resume();
        drive(session, fake, clock, 2000);
        QVERIFY(recorder.samples().size() > recorded_before);
    }

    void disconnectMidWorkoutPausesWithError() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 2000);
        QCOMPARE(session.state(), SessionState::Running);
        // Trainer disconnects mid-workout.
        fake->disconnect();
        drive(session, nullptr, clock, 2000);
        QCOMPARE(session.state(), SessionState::PausedWithError);
        // The file is still open (recorder not finished), so
        // the recorded data so far is safe.
        QVERIFY(!recorder.finished());
        QVERIFY(!recorder.samples().empty());
        // Resume reconnects and resumes.
        session.resume();
        QCOMPARE(session.state(), SessionState::Running);
        QVERIFY(fake->isConnected());
        drive(session, fake, clock, 2000);
        QCOMPARE(session.state(), SessionState::Running);
    }

    void dropoutIsInvalidNotZero() {
        FakeClock clock;
        InMemoryRecorder recorder;
        RecordingObserver observer;
        // A scripted device with a dropout in the middle.
        // Stale timeout is shortened to 500 ms so the dropout
        // (1 s, from 1000 to 2000 ms) actually goes stale
        // between recorded samples; the observer sees every
        // tick, so it catches the invalid window that the
        // 1 Hz recorder would sample around.
        TrainSession session;
        session.setClock(&clock);
        session.setRecorder(&recorder);
        session.setStaleTimeoutMs(500);
        session.subscribe(&observer);
        AthleteProfile profile;
        profile.ftp_watts = 250;
        session.setAthleteProfile(profile);
        FakeDeviceConfig config;
        config.name = "power";
        config.is_trainer = false;
        TelemetrySample s1, s2, s3;
        s1.monotonic_ms = 0; s1.watts = 200; s1.power_valid = true;
        s2.monotonic_ms = 1000; s2.watts = 200; s2.power_valid = true;
        s3.monotonic_ms = 2000; s3.watts = 200; s3.power_valid = true;
        config.script = {s1, s2, s3};
        config.dropouts = {{1000, 2000}}; // drop the middle sample
        auto fake_ptr = std::make_unique<FakeDevice>(config);
        FakeDevice *fake = fake_ptr.get();
        session.addDevice(std::move(fake_ptr));
        session.start();
        drive(session, fake, clock, 3000);
        // The session did not crash, and the merged samples
        // during the dropout window are invalid, not zero:
        // power_valid false with the last known watts, per
        // Phase 2 merge rules.
        QVERIFY(!observer.telemetry.empty());
        bool saw_invalid = false;
        for (const auto &sample : observer.telemetry) {
            if (!sample.power_valid) {
                saw_invalid = true;
                QCOMPARE(sample.watts, 200.0); // last known, not zero
            }
        }
        QVERIFY(saw_invalid);
    }

    void recordingAt1HzNotTickRate() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        // 10 seconds at 250 ms ticks = 40 ticks, but only
        // about 10 recorded samples (1 Hz), not 40.
        drive(session, fake, clock, 10000);
        QVERIFY(recorder.samples().size() >= 9);
        QVERIFY(recorder.samples().size() <= 11);
    }

    void stepChangeEvents() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        RecordingObserver observer;
        session.subscribe(&observer);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 65000);
        // Step 0 then step 1, each fired once. (Driving past
        // 120 s would add a final -1 event for workout end,
        // which is correct but not what this test asserts.)
        QVERIFY(observer.steps.size() >= 2);
        QVERIFY(observer.steps.back() == 1);
    }

    void lapRecords() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.start();
        drive(session, fake, clock, 2000);
        session.lap();
        drive(session, fake, clock, 2000);
        session.lap();
        QCOMPARE(recorder.laps().size(), size_t(2));
    }

    // ---------------------------------------------------- live metrics
    void liveMetricsConstantPower() {
        std::vector<PowerHistoryEntry> history;
        for (int i = 0; i <= 600; ++i) {
            history.push_back({i * 1000, 250, true});
        }
        AthleteProfile profile;
        profile.ftp_watts = 250;
        LiveMetrics metrics = computeLiveMetrics(history, 600000, profile);
        QCOMPARE(metrics.watts_3s, 250.0);
        QVERIFY(std::abs(metrics.normalized_power - 250.0) < 0.001);
        QCOMPARE(metrics.wbal_joules, 20000.0);
        QVERIFY(std::abs(metrics.tss_so_far - 16.6667) < 0.01);
    }

    void liveMetricsWbalFallsAboveCp() {
        std::vector<PowerHistoryEntry> history;
        for (int i = 0; i <= 100; ++i) {
            history.push_back({i * 1000, 300, true});
        }
        double wbal_value = wbal(history, 250, 20000);
        QVERIFY(std::abs(wbal_value - 15000.0) < 0.001);
    }

    void sessionLiveMetricsOnTick() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 10000);
        LiveMetrics metrics = session.liveMetrics();
        // After 10 s converging toward 250 W, 3s power is
        // positive and below the target (convergence lag),
        // and the recorded sample also carries watts_3s.
        // (They are not compared for equality: the last
        // recorded sample is from the last 1 Hz record time,
        // while liveMetrics is from the last 250 ms tick, so
        // the two 3s values legitimately differ.)
        QVERIFY(metrics.watts_3s > 0);
        QVERIFY(metrics.watts_3s <= 250.0);
        QVERIFY(!recorder.samples().empty());
        QVERIFY(recorder.samples().back().watts_3s > 0);
        QVERIFY(recorder.samples().back().watts_3s <= 250.0);
    }

    // --------------------------------------------------- integration
    void fullWorkoutIntegration() {
        FakeClock clock;
        InMemoryRecorder recorder;
        FakeDevice *fake = nullptr;
        TrainSession session = makeSession(clock, recorder, fake);
        session.loadWorkout(twoStepWorkout());
        session.start();
        drive(session, fake, clock, 125000);
        session.stopAndSave();
        QVERIFY(recorder.finished());
        // 120 s of workout at 1 Hz, plus the first tick.
        QVERIFY(recorder.samples().size() >= 115);
        QVERIFY(recorder.samples().size() <= 125);
        // Interval averages near targets (ERG convergence
        // means the first seconds of each step are below
        // target, so use a generous tolerance and the second
        // half of each step).
        double sum1 = 0, sum2 = 0;
        int count1 = 0, count2 = 0;
        for (const auto &sample : recorder.samples()) {
            if (sample.monotonic_ms >= 30000 &&
                sample.monotonic_ms < 60000) {
                sum1 += sample.watts;
                ++count1;
            } else if (sample.monotonic_ms >= 90000) {
                sum2 += sample.watts;
                ++count2;
            }
        }
        QVERIFY(count1 > 0 && count2 > 0);
        double avg1 = sum1 / count1;
        double avg2 = sum2 / count2;
        // Step 1 converges up to 250 W, so its second-half
        // average is just below 250. Step 2 converges down
        // from 250 W to 150 W, so its second-half average is
        // just above 150 (about 153 W: the exponential tail
        // adds roughly 100 W * tau/duration = 3 W).
        QVERIFY(avg1 > 200.0 && avg1 <= 250.0);
        QVERIFY(avg2 > 140.0 && avg2 < 160.0);
    }
};

QTEST_MAIN(TestTrainSession)
#include "testTrainSession.moc"
