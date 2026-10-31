// Phase 1 tests: WorkoutPlayer and ERG/MRC parser.
//
// No sleep() anywhere: all time is driven by FakeClock, and the
// player is driven by passing FakeClock::nowMonotonicMs() as
// now_monotonic_ms. That is the TDD 7.6 contract — time is a
// parameter, not a global clock or a UI timer.
//
// The player tests use a 3-step workout: 60 s @ 250 W, 60 s @ 80%
// of FTP, 60 s slope 5%. FTP is 250 W, so the percent step resolves
// to 200 W. Total duration is 180 s.
//
// The parser tests use embedded fixture strings, not files from
// test/workouts/, so the suite runs from any working directory
// (unlike seasonParser, which needs seasons.xml beside it).

#include "Core/WorkoutPlayer.h"
#include "Core/WorkoutParser.h"
#include "Core/Clock.h"

#include <QTest>

static Workout threeStepWorkout() {
    Workout workout;
    workout.name = "3-step";
    workout.steps = {
        {60, 250, 0, 0, "erg"},
        {60, 0, 80, 0, "pct"},
        {60, 0, 0, 5, "slope"},
    };
    return workout;
}

class TestWorkoutPlayer : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- player
    void loadAndIdle() {
        WorkoutPlayer player;
        QVERIFY(!player.hasWorkout());
        QVERIFY(player.state() == WorkoutState::Idle);
        QVERIFY(player.tick(0).mode == TrainerCommand::Mode::Idle);
        QCOMPARE(player.currentStepIndex(0), -1);

        player.load(threeStepWorkout(), 250);
        QVERIFY(player.hasWorkout());
        QVERIFY(player.state() == WorkoutState::Idle);
        QCOMPARE(player.currentStepIndex(0), -1);
        QCOMPARE(player.elapsedSecs(0), 0.0);
        QCOMPARE(player.timeRemainingSecs(0), 180.0);
    }

    void tickTargetsPerSecond() {
        // TDD Phase 1 acceptance, in miniature: load, tick a fake
        // clock, assert targets at each step.
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());

        // Step 0: 250 W, from t=0 to t=60 s (exclusive).
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 250.0);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 0);
        clock.advanceMs(30000);
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 250.0);
        QCOMPARE(player.timeInStepSecs(clock.nowMonotonicMs()), 30.0);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 30.0);
        QCOMPARE(player.timeRemainingSecs(clock.nowMonotonicMs()), 150.0);

        // Step boundary at exactly 60 s: next step is current.
        clock.advanceMs(30000);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 1);
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 200.0); // 80% of 250
        QCOMPARE(player.currentTargetWatts(clock.nowMonotonicMs()), 200.0);

        // Step 2: slope, not watts.
        clock.advanceMs(60000);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 2);
        TrainerCommand slope = player.tick(clock.nowMonotonicMs());
        QVERIFY(slope.mode == TrainerCommand::Mode::Slope);
        QCOMPARE(slope.gradient_pct, 5.0);
        QCOMPARE(player.currentTargetWatts(clock.nowMonotonicMs()), 0.0);
    }

    void wattsPrecedenceOverPercent() {
        Workout workout;
        workout.steps = {{60, 300, 80, 0, "both"}};
        WorkoutPlayer player;
        player.load(workout, 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        // Watts wins when both are set, per Workout precedence.
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 300.0);
    }

    void pauseFreezesTime() {
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        clock.advanceMs(30000);
        player.pause(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Paused);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 30.0);
        // Wall (fake) time moves, player time does not, and tick is Idle.
        clock.advanceMs(60000);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 30.0);
        QVERIFY(player.tick(clock.nowMonotonicMs()).mode ==
                  TrainerCommand::Mode::Idle);
        // Resume continues from 30 s, not from 90 s.
        player.resume(clock.nowMonotonicMs());
        clock.advanceMs(30000);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 60.0);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 1);
        // Pause/resume on the wrong state are no-ops, not crashes.
        player.pause(clock.nowMonotonicMs());
        player.pause(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Paused);
        player.resume(clock.nowMonotonicMs());
        player.resume(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Running);
    }

    void skipStep() {
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        clock.advanceMs(10000);
        player.skipStep(clock.nowMonotonicMs());
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 1);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 60.0);
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 200.0);
        // Skipping the last step finishes the workout.
        player.skipStep(clock.nowMonotonicMs());
        player.skipStep(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Finished);
        QVERIFY(player.tick(clock.nowMonotonicMs()).mode ==
                  TrainerCommand::Mode::Idle);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), -1);
        QCOMPARE(player.timeRemainingSecs(clock.nowMonotonicMs()), 0.0);
    }

    void seek() {
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        player.seek(130, clock.nowMonotonicMs());
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 130.0);
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 2);
        QCOMPARE(player.timeInStepSecs(clock.nowMonotonicMs()), 10.0);
        // Seek past the end finishes; seek below 0 clamps to 0.
        player.seek(9999, clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Finished);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 180.0);
        // Seek on a finished player is a no-op (not a restart).
        player.seek(10, clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Finished);
    }

    void seekWhilePausedStaysPaused() {
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        clock.advanceMs(10000);
        player.pause(clock.nowMonotonicMs());
        player.seek(130, clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Paused);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 130.0);
        player.resume(clock.nowMonotonicMs());
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), 2);
    }

    void finishAtTotal() {
        WorkoutPlayer player;
        player.load(threeStepWorkout(), 250);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        clock.advanceMs(180000);
        // Time-aware getters already report finished, even before
        // tick runs (see WorkoutPlayer.h state() note).
        QCOMPARE(player.currentStepIndex(clock.nowMonotonicMs()), -1);
        QCOMPARE(player.timeRemainingSecs(clock.nowMonotonicMs()), 0.0);
        QVERIFY(player.tick(clock.nowMonotonicMs()).mode ==
                  TrainerCommand::Mode::Idle);
        QVERIFY(player.state() == WorkoutState::Finished);
        // start() restarts from the beginning.
        player.start(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Running);
        QCOMPARE(player.elapsedSecs(clock.nowMonotonicMs()), 0.0);
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 250.0);
    }

    void emptyWorkoutFinishesOnStart() {
        Workout empty;
        WorkoutPlayer player;
        player.load(empty, 250);
        QVERIFY(player.hasWorkout());
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        QVERIFY(player.state() == WorkoutState::Finished);
        QVERIFY(player.tick(clock.nowMonotonicMs()).mode ==
                  TrainerCommand::Mode::Idle);
    }

    // ---------------------------------------------------------- parser
    void parseErgAbsoluteWatts() {
        const std::string content =
            "[COURSE HEADER]\n"
            "FTP = 260\n"
            "DESCRIPTION = Test workout\n"
            "MINUTES  WATTS\n"
            "[END COURSE HEADER]\n"
            "[COURSE DATA]\n"
            "0.0\t100\n"
            "2.0\t100\n"
            "2.0\t200\n"
            "5.0\t200\n";
        WorkoutParseResult result = parseWorkout(content, "test.erg");
        QVERIFY(result.ok);
        QCOMPARE(result.workout.name, std::string("Test workout"));
        QCOMPARE(result.workout.source_path, std::string("test.erg"));
        QCOMPARE(result.workout.steps.size(), size_t(2));
        QCOMPARE(result.workout.steps[0].duration_secs, 120.0);
        QCOMPARE(result.workout.steps[0].target_watts, 100.0);
        QCOMPARE(result.workout.steps[1].duration_secs, 180.0);
        QCOMPARE(result.workout.steps[1].target_watts, 200.0);
        QCOMPARE(result.workout.total_duration_secs(), 300.0);
    }

    void parseMrcPercent() {
        const std::string content =
            "[COURSE HEADER]\n"
            "MINUTES  PERCENT\n"
            "[END COURSE HEADER]\n"
            "[COURSE DATA]\n"
            "0.0 50\n"
            "10.0 50\n"
            "10.0 150\n"
            "20.0 150\n";
        WorkoutParseResult result = parseWorkout(content, "test.mrc");
        QVERIFY(result.ok);
        QCOMPARE(result.workout.steps.size(), size_t(2));
        QCOMPARE(result.workout.steps[0].target_pct_ftp, 50.0);
        QCOMPARE(result.workout.steps[0].target_watts, 0.0);
        QCOMPARE(result.workout.steps[1].target_pct_ftp, 150.0);
        // And the player resolves it against load-time FTP, not
        // against any header FTP (there is none here).
        WorkoutPlayer player;
        player.load(result.workout, 200);
        FakeClock clock;
        player.start(clock.nowMonotonicMs());
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 100.0);
        clock.advanceMs(600000);
        QCOMPARE(player.tick(clock.nowMonotonicMs()).target_watts, 300.0);
    }

    void parseMergesIdenticalSteps() {
        const std::string content =
            "[COURSE HEADER]\n"
            "MINUTES  WATTS\n"
            "[END COURSE HEADER]\n"
            "[COURSE DATA]\n"
            "0.0 100\n"
            "5.0 100\n"
            "10.0 100\n";
        WorkoutParseResult result = parseWorkout(content, "merge.erg");
        QVERIFY(result.ok);
        QCOMPARE(result.workout.steps.size(), size_t(1));
        QCOMPARE(result.workout.steps[0].duration_secs, 600.0);
        QCOMPARE(result.workout.total_duration_secs(), 600.0);
    }

    void parseNameFallsBackToFilename() {
        const std::string content =
            "[COURSE HEADER]\n"
            "MINUTES  WATTS\n"
            "[END COURSE HEADER]\n"
            "[COURSE DATA]\n"
            "0.0 100\n"
            "1.0 100\n";
        WorkoutParseResult result =
            parseWorkout(content, "workouts/my-workout.erg");
        QVERIFY(result.ok);
        QCOMPARE(result.workout.name, std::string("my-workout.erg"));
    }

    void parseErrors() {
        QVERIFY(!parseWorkout("", "empty.erg").ok);
        QVERIFY(!parseWorkout("[COURSE HEADER]\nMINUTES  WATTS\n"
                              "[END COURSE HEADER]\n",
                              "nodata.erg")
                     .ok);
        QVERIFY(!parseWorkout("[COURSE DATA]\nhello\n", "bad.erg").ok);
        WorkoutParseResult crs = parseWorkout(
            "[COURSE HEADER]\nDISTANCE  GRADE  WIND\n[END COURSE HEADER]\n"
            "[COURSE DATA]\n0 0 0\n",
            "route.crs");
        QVERIFY(!crs.ok);
        QVERIFY(crs.error.find("CRS") != std::string::npos);
        WorkoutParseResult outOfOrder = parseWorkout(
            "[COURSE HEADER]\nMINUTES  WATTS\n[END COURSE HEADER]\n"
            "[COURSE DATA]\n5.0 100\n2.0 100\n",
            "order.erg");
        QVERIFY(!outOfOrder.ok);
    }

    void parseRealErgShape() {
        // Shape taken from test/workouts/1x10x30s@128%.erg (first
        // minutes): warm-up steps, then a step change at the same
        // minute mark. This pins the same-time change-point rule
        // against a real file's structure, without depending on
        // that file being present at test time.
        const std::string content =
            "[COURSE HEADER]\n"
            "FTP = 260\n"
            "DESCRIPTION = 1x10x30 second efforts at 128% of FTP.\n"
            "MINUTES  WATTS\n"
            "[END COURSE HEADER]\n"
            "[COURSE DATA]\n"
            "0.0\t78\n"
            "1.0\t78\n"
            "1.0\t104\n"
            "2.0\t104\n"
            "2.0\t143\n"
            "10.0\t143\n";
        WorkoutParseResult result = parseWorkout(content, "real.erg");
        QVERIFY(result.ok);
        QCOMPARE(result.workout.steps.size(), size_t(3));
        QCOMPARE(result.workout.steps[0].duration_secs, 60.0);
        QCOMPARE(result.workout.steps[0].target_watts, 78.0);
        QCOMPARE(result.workout.steps[1].duration_secs, 60.0);
        QCOMPARE(result.workout.steps[1].target_watts, 104.0);
        QCOMPARE(result.workout.steps[2].duration_secs, 480.0);
        QCOMPARE(result.workout.steps[2].target_watts, 143.0);
    }
};

QTEST_MAIN(TestWorkoutPlayer)
#include "testWorkoutPlayer.moc"
