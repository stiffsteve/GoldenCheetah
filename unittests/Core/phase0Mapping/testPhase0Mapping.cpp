// Phase 0 mapping tests (core/view split, TDD sections 7 and 14).
//
// These tests prove the mapping tables from the inventory
// (doc/core-view-split/phase0-inventory.md, section 2) BEFORE any
// production call site moves. No production code is migrated here.
//
// WHY FIXTURES, NOT THE OLD OBJECTS
// ---------------------------------
// The obvious test would construct a TelemetrySample from a real
// RealtimeData, a Workout from a real ErgFile, and an AthleteProfile
// from a real ZoneRange. That cannot be done cleanly in Phase 0:
//   - RealtimeData.h includes GoldenCheetah.h, ErgFileBase.h, and
//     <QApplication>, and RealtimeData.o contains RealtimeDataSession,
//     which references Context, Athlete, and RideFile — linking it
//     drags the God Context and the GUI into a core test.
//   - ErgFile.h includes Context.h, Zones.h, Settings.h, <QtGui>,
//     and <QTextEdit>.
//   - Zones.h includes GoldenCheetah.h and Athlete.h.
// Including any of them in a core test would defeat the purpose of
// Phase 0. So these tests use plain fixtures that mirror only the
// field values of the old types, and test-local mapping functions
// that implement the mapping tables.
//
// The test-local mapping functions below are TEST-ONLY PROTOTYPES.
// Phase 0 step 5 promotes them to src/Core, once the old types can be
// included without pulling Widgets/Context — or, equivalently, once
// the mapping is done at a boundary where the old types are already
// available. When that promotion happens, these tests should be
// switched to call the production mapping functions, with the
// assertions unchanged. That switch is the step 5 exit proof.
//
// The new types themselves are pure standard C++ (see src/Core/),
// so this test only needs QtTest for the test harness itself.

#include "Core/TelemetrySample.h"
#include "Core/DeviceInfo.h"
#include "Core/SessionState.h"
#include "Core/Workout.h"
#include "Core/TrainerCommand.h"
#include "Core/AthleteProfile.h"
#include "Core/Clock.h"

#include <QTest>

// ---------------------------------------------------------------- fixtures
// Mirrors only the field values used by the mapping tables. Field
// names and units follow the old types; see the inventory for the
// full old-type shapes.

struct RealtimeFixture {
    double watts = 0;
    double heartrate = 0;
    double cadence = 0;
    double speed_kph = 0;
    double distance_m = 0;
    double altitude_m = 0;
    double slope_pct = 0;      // RealtimeData slope
    double load_pct = 0;       // RealtimeData load
    double target_watts = 0;
    int64_t msecs = 0;         // RealtimeData time, in milliseconds
    bool has_power = false;    // source present, not "watts != 0"
    bool has_hr = false;
    bool trainer_connected = false;
};

struct ErgSectionFixture {
    int duration_ms = 0;       // ErgFileSection duration
    double start_value = 0;    // watts in ERG mode, gradient in Slope mode
    bool is_slope = false;
    std::string label;
};

struct ZoneRangeFixture {
    int ftp = 0;               // Zones::ZoneRange ftp
    double weight_kg = 75;     // stored athlete weight (not in ZoneRange)
    double bike_weight_kg = 8;
    double height_cm = 175;
};

// ------------------------------------------------------- test-only mapping
// Prototype of the Phase 0 mapping tables. See the header comment:
// promote these to src/Core in step 5, and switch these tests to
// call them, keeping the assertions unchanged.

static TelemetrySample toTelemetrySample(const RealtimeFixture &rt,
                                         int64_t wallclock_ms) {
    TelemetrySample sample;
    sample.monotonic_ms = rt.msecs;
    sample.wallclock_ms = wallclock_ms;
    sample.watts = rt.watts;
    // Devices never set watts_3s; the session computes it (TDD 7.1).
    sample.watts_3s = 0;
    sample.heartrate = rt.heartrate;
    sample.cadence = rt.cadence;
    sample.speed_kph = rt.speed_kph;
    sample.distance_m = rt.distance_m;
    sample.altitude_m = rt.altitude_m;
    sample.gradient_pct = rt.slope_pct;
    sample.target_watts = rt.target_watts;
    // Validity is explicit and independent of the value: a dropout
    // is invalid data, not zero watts (TDD 7.1).
    sample.power_valid = rt.has_power;
    sample.hr_valid = rt.has_hr;
    sample.trainer_connected = rt.trainer_connected;
    sample.trainer_load_pct = static_cast<int>(rt.load_pct);
    return sample;
}

static WorkoutStep toWorkoutStep(const ErgSectionFixture &section) {
    WorkoutStep step;
    step.duration_secs = section.duration_ms / 1000.0;
    if (section.is_slope) {
        step.target_gradient_pct = section.start_value;
    } else {
        step.target_watts = section.start_value;
    }
    step.label = section.label;
    return step;
}

static AthleteProfile toAthleteProfile(const ZoneRangeFixture &range) {
    AthleteProfile profile;
    profile.ftp_watts = range.ftp;
    profile.weight_kg = range.weight_kg;
    profile.bike_weight_kg = range.bike_weight_kg;
    profile.height_cm = range.height_cm;
    return profile;
}

// ------------------------------------------------------------------- tests

class TestPhase0Mapping : public QObject {
    Q_OBJECT

private slots:
    void telemetrySampleDefaults() {
        TelemetrySample sample;
        QCOMPARE(sample.monotonic_ms, int64_t(0));
        QCOMPARE(sample.wallclock_ms, int64_t(0));
        QCOMPARE(sample.watts, 0.0);
        QCOMPARE(sample.watts_3s, 0.0);
        QCOMPARE(sample.power_valid, false);
        QCOMPARE(sample.hr_valid, false);
        QCOMPARE(sample.trainer_connected, false);
        QCOMPARE(sample.trainer_load_pct, 0);
    }

    void telemetrySampleMapping() {
        RealtimeFixture rt;
        rt.watts = 250;
        rt.heartrate = 140;
        rt.cadence = 88;
        rt.speed_kph = 36.5;
        rt.distance_m = 12345;
        rt.altitude_m = 210;
        rt.slope_pct = 4.5;
        rt.load_pct = 62;
        rt.target_watts = 260;
        rt.msecs = 123456;
        rt.has_power = true;
        rt.has_hr = true;
        rt.trainer_connected = true;

        TelemetrySample sample = toTelemetrySample(rt, 1700000000000LL);
        QCOMPARE(sample.monotonic_ms, int64_t(123456));
        QCOMPARE(sample.wallclock_ms, int64_t(1700000000000LL));
        QCOMPARE(sample.watts, 250.0);
        // Session-derived, not device-derived: mapping must leave it 0.
        QCOMPARE(sample.watts_3s, 0.0);
        QCOMPARE(sample.heartrate, 140.0);
        QCOMPARE(sample.cadence, 88.0);
        QCOMPARE(sample.speed_kph, 36.5);
        QCOMPARE(sample.distance_m, 12345.0);
        QCOMPARE(sample.altitude_m, 210.0);
        QCOMPARE(sample.gradient_pct, 4.5);
        QCOMPARE(sample.target_watts, 260.0);
        QCOMPARE(sample.power_valid, true);
        QCOMPARE(sample.hr_valid, true);
        QCOMPARE(sample.trainer_connected, true);
        QCOMPARE(sample.trainer_load_pct, 62);
    }

    void telemetrySampleDropoutIsNotZero() {
        // The key TDD rule: a dropout is invalid data, not zero watts.
        // A sample with watts == 0 and power_valid == false must not
        // be read as "the rider produced 0 W".
        RealtimeFixture rt;
        rt.watts = 0;
        rt.heartrate = 0;
        rt.has_power = false;
        rt.has_hr = false;

        TelemetrySample sample = toTelemetrySample(rt, 0);
        QCOMPARE(sample.watts, 0.0);
        QCOMPARE(sample.power_valid, false);
        QCOMPARE(sample.hr_valid, false);
        // Conversely, a present source with a genuine 0 W reading is
        // valid data, and must be distinguishable from a dropout.
        rt.has_power = true;
        sample = toTelemetrySample(rt, 0);
        QCOMPARE(sample.power_valid, true);
    }

    void deviceInfoIsAValueNotAPointer() {
        DeviceInfo trainer;
        trainer.id = "fake-trainer-1";
        trainer.name = "Fake Trainer";
        trainer.transport = "virtual";
        trainer.is_controllable_trainer = true;
        trainer.has_power = true;
        trainer.has_hr = false;
        trainer.has_speed_cadence = true;
        // A smart trainer is both a sensor and a controllable device:
        // the two roles must be able to coexist on one DeviceInfo.
        QVERIFY(trainer.is_controllable_trainer);
        QVERIFY(trainer.has_power);
        QCOMPARE(trainer.id, std::string("fake-trainer-1"));
        QCOMPARE(trainer.transport, std::string("virtual"));

        DeviceInfo hrStrap;
        hrStrap.id = "ble-hr-1";
        hrStrap.transport = "ble";
        hrStrap.has_hr = true;
        QVERIFY(!hrStrap.is_controllable_trainer);
        QVERIFY(hrStrap.has_hr);
    }

    void workoutTotalDuration() {
        Workout empty;
        QCOMPARE(empty.total_duration_secs(), 0.0);

        Workout workout;
        workout.name = "3x10";
        workout.source_path = "workouts/3x10.erg";
        workout.steps = {
            {600, 250, 0, 0, "interval 1"},
            {300, 150, 0, 0, "rest"},
            {600, 250, 0, 0, "interval 2"},
        };
        QCOMPARE(workout.total_duration_secs(), 1500.0);
    }

    void workoutStepMappingErg() {
        ErgSectionFixture section;
        section.duration_ms = 600000;
        section.start_value = 250;
        section.is_slope = false;
        section.label = "interval";

        WorkoutStep step = toWorkoutStep(section);
        QCOMPARE(step.duration_secs, 600.0);
        QCOMPARE(step.target_watts, 250.0);
        QCOMPARE(step.target_gradient_pct, 0.0);
        QCOMPARE(step.label, std::string("interval"));
    }

    void workoutStepMappingSlope() {
        ErgSectionFixture section;
        section.duration_ms = 300000;
        section.start_value = 6.5;
        section.is_slope = true;

        WorkoutStep step = toWorkoutStep(section);
        QCOMPARE(step.duration_secs, 300.0);
        QCOMPARE(step.target_watts, 0.0);
        QCOMPARE(step.target_gradient_pct, 6.5);
    }

    void workoutStepPrecedenceIsPreservedInTheType() {
        // TDD 7.2: target_watts takes precedence if > 0, otherwise
        // target_pct_ftp is used. The type itself preserves both
        // values; resolution is WorkoutPlayer's job (Phase 1), not
        // the type's. This test pins the preservation, so a later
        // player implementation cannot silently drop one of them.
        WorkoutStep step;
        step.duration_secs = 600;
        step.target_watts = 250;
        step.target_pct_ftp = 90;
        QCOMPARE(step.target_watts, 250.0);
        QCOMPARE(step.target_pct_ftp, 90.0);

        WorkoutStep pctOnly;
        pctOnly.duration_secs = 600;
        pctOnly.target_pct_ftp = 90;
        QCOMPARE(pctOnly.target_watts, 0.0);
        QCOMPARE(pctOnly.target_pct_ftp, 90.0);
    }

    void trainerCommandModes() {
        TrainerCommand erg;
        erg.mode = TrainerCommand::Mode::Erg;
        erg.target_watts = 250;
        QVERIFY(erg.mode == TrainerCommand::Mode::Erg);
        QCOMPARE(erg.target_watts, 250.0);

        TrainerCommand slope;
        slope.mode = TrainerCommand::Mode::Slope;
        slope.gradient_pct = 5.0;
        QVERIFY(slope.mode == TrainerCommand::Mode::Slope);

        TrainerCommand resistance;
        resistance.mode = TrainerCommand::Mode::Resistance;
        resistance.resistance_0_100 = 50;
        QCOMPARE(resistance.resistance_0_100, 50);

        TrainerCommand idle;
        QVERIFY(idle.mode == TrainerCommand::Mode::Idle);
        QVERIFY(idle.mode != TrainerCommand::Mode::Sim);
    }

    void sessionStateValuesAreDistinct() {
        // The mapping from trainer booleans / controller return codes
        // to SessionState (inventory section 2.5) is only meaningful
        // if the values are distinct. Pin that here.
        QVERIFY(SessionState::Idle != SessionState::Ready);
        QVERIFY(SessionState::Ready != SessionState::Running);
        QVERIFY(SessionState::Running != SessionState::Paused);
        QVERIFY(SessionState::Paused != SessionState::PausedWithError);
        QVERIFY(SessionState::PausedWithError != SessionState::Finished);
        QVERIFY(SessionState::Finished != SessionState::Error);
    }

    void athleteProfileDefaults() {
        AthleteProfile profile;
        QCOMPARE(profile.ftp_watts, 250.0);
        QCOMPARE(profile.weight_kg, 75.0);
        QCOMPARE(profile.bike_weight_kg, 8.0);
        QCOMPARE(profile.height_cm, 175.0);
    }

    void athleteProfileMappingFromZoneRange() {
        // FTP is per ZoneRange in v3.8, not a single number. The GUI
        // layer must pick the range (and sport) explicitly; this test
        // pins the mapping once that choice is made.
        ZoneRangeFixture range;
        range.ftp = 280;
        range.weight_kg = 75;
        range.bike_weight_kg = 8;
        range.height_cm = 175;

        AthleteProfile profile = toAthleteProfile(range);
        QCOMPARE(profile.ftp_watts, 280.0);
        QCOMPARE(profile.weight_kg, 75.0);
    }

    void clockFakeAdvance() {
        FakeClock clock;
        QCOMPARE(clock.nowMonotonicMs(), int64_t(0));
        QCOMPARE(clock.nowWallclockMs(), int64_t(0));
        clock.advanceMs(250);
        QCOMPARE(clock.nowMonotonicMs(), int64_t(250));
        QCOMPARE(clock.nowWallclockMs(), int64_t(250));
        clock.advanceMs(250);
        QCOMPARE(clock.nowMonotonicMs(), int64_t(500));
        // Independent setters (extension beyond the TDD, see Clock.h).
        clock.setMonotonicMs(1000);
        QCOMPARE(clock.nowMonotonicMs(), int64_t(1000));
        QCOMPARE(clock.nowWallclockMs(), int64_t(500));
        clock.setWallclockMs(2000);
        QCOMPARE(clock.nowWallclockMs(), int64_t(2000));
    }

    void clockSystemIsNonDecreasing() {
        SystemClock clock;
        int64_t first = clock.nowMonotonicMs();
        int64_t second = clock.nowMonotonicMs();
        QVERIFY(second >= first);
        QVERIFY(clock.nowWallclockMs() > 0);
    }
};

QTEST_MAIN(TestPhase0Mapping)
#include "testPhase0Mapping.moc"
