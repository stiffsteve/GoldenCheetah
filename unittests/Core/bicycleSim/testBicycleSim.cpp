// BicycleSim core port: pin the moved model against the probe
// numbers measured from the GUI model before the port
// (doc/core-view-split/bicyclesim-scope.md, "Probe results").
//
// The suite drives the PORTED model with the dt-explicit overload
// and fixed 100 ms steps — deterministic, no sleeps (TDD 15.4).
// The probe used wall-clock dt (~100 ms/tick with jitter), so the
// integrated traces agree within tolerance, not exactly; the
// pure-function pins and the filter-clamp equality are exact.
//
// Configuration: 75 kg rider, default parts (SimBicyclePage
// defaults). Total mass 81.48 kg, KE mass 82.545012 kg. The
// steady-state oracle is computeInstantSpeed at the sim's mass
// (81.48 kg) and Cd (0.9955): 250 W flat 32.152, 150 W flat
// 26.603, 250 W at 8% 12.680 kph.

#include "Core/BicycleSim.h"
#include "Core/PhysicsUtility.h"

#include <QTest>
#include <cmath>

namespace {

// Fixed 100 ms steps, as the probe used.
const double kDt = 0.1;

// Drive `ticks` ticks at constant inputs; return the last
// SpeedDistance, accumulating distance in `distKm`.
SpeedDistance drive(Bicycle &bike, double watts, double slope,
                    double altitude, int ticks, double &distKm) {
    SpeedDistance last{0, 0};
    for (int i = 0; i < ticks; i++) {
        BicycleSimState state(watts, slope, altitude);
        last = bike.SampleSpeed(state, kDt);
        distKm += last.d;
    }
    return last;
}

Bicycle makeBike(double riderKg = 75) {
    BicycleConfig config;
    config.rider_weight_kg = riderKg;
    return Bicycle(config);
}

void checkSpeed(double actual, double expected, double tol = 0.5) {
    QVERIFY2(std::abs(actual - expected) < tol,
             qPrintable(QString("speed %1, expected %2 (tol %3)")
                            .arg(actual).arg(expected).arg(tol)));
}

void checkDistM(double actualM, double expectedM) {
    // Integrated distance: ±2% (the probe's wall-clock dt jitters).
    QVERIFY2(std::abs(actualM - expectedM) < expectedM * 0.02,
             qPrintable(QString("distance %1 m, expected %2 m")
                            .arg(actualM).arg(expectedM)));
}

void checkOracle(double actual, double oracle) {
    // Steady state must equal the instant model within ±0.5%.
    QVERIFY2(std::abs(actual - oracle) < oracle * 0.005,
             qPrintable(QString("speed %1, oracle %2").arg(actual).arg(oracle)));
}

} // namespace

class TestBicycleSim : public QObject {
    Q_OBJECT

private slots:
    void massAndWheels() {
        Bicycle bike = makeBike();
        QVERIFY(std::abs(bike.MassKG() - 81.48) < 1e-9);
        QVERIFY(std::abs(bike.KEMass() - 82.545012) < 1e-6);
        QVERIFY(std::abs(bike.FrontWheel().EquivalentMassKG() - 0.532506) < 1e-6);
        QVERIFY(std::abs(bike.RearWheel().EquivalentMassKG() - 0.532506) < 1e-6);
        QVERIFY(std::abs(bike.FrontWheel().MassKG() - 1.145) < 1e-9);
        QVERIFY(std::abs(bike.RearWheel().MassKG() - 1.335) < 1e-9);
    }

    void defaultCtorIs80KgRider() {
        Bicycle bike;
        QVERIFY(std::abs(bike.MassKG() - 86.48) < 1e-9);
        QVERIFY(std::abs(bike.KEMass() - 87.545012) < 1e-6);
    }

    void configureReplacesConfig() {
        Bicycle bike; // 80 kg rider
        BicycleConfig config;
        config.rider_weight_kg = 75;
        bike.configure(config);
        QVERIFY(std::abs(bike.MassKG() - 81.48) < 1e-9);
    }

    void keRoundTrip() {
        Bicycle bike = makeBike();
        for (double v : {0.0, 1.0, 5.0, 10.0, 15.0}) {
            double back = bike.VFromKE(bike.KEFromV(v));
            QVERIFY(std::abs(back - v) < 1e-9);
        }
    }

    void wattsForVIsInverse() {
        Bicycle bike = makeBike();
        // Same force model as computeInstantSpeed, compared at the
        // sim's mass (81.48 kg) and Cd (0.9955). Probe: all 60 grid
        // points agree to 0.000000000 W.
        for (double watts : {100.0, 150.0, 200.0, 250.0, 300.0}) {
            for (double slope : {0.0, 5.0, 8.0, -5.0}) {
                for (double alt : {0.0, 1000.0, 2000.0}) {
                    double v = computeInstantSpeed(81.48, slope, alt, watts,
                                                   0.004, 1.0, 1.0 - 0.0045,
                                                   0.5, 293.15);
                    BicycleSimState state(watts, slope, alt);
                    double wfv = bike.WattsForV(state, v / 3.6);
                    QVERIFY2(std::abs(wfv - watts) < 1e-6,
                             qPrintable(QString("WattsForV %1 at %2 W, "
                                               "slope %3, alt %4")
                                            .arg(wfv).arg(watts)
                                            .arg(slope).arg(alt)));
                }
            }
        }
    }

    void steadyState250Flat() {
        Bicycle bike = makeBike();
        double distKm = 0;
        // Speeds at 5/10/20/30/60 s (ticks 50/100/200/300/600).
        const double expected[] = {17.261, 23.688, 29.265, 31.161, 32.121};
        const int ticks[] = {50, 100, 200, 300, 600};
        int done = 0;
        SpeedDistance last{0, 0};
        for (int i = 0; i < 5; i++) {
            last = drive(bike, 250, 0, 0, ticks[i] - done, distKm);
            done = ticks[i];
            checkSpeed(last.v, expected[i]);
        }
        checkDistM(distKm * 1000, 469.378);
        checkOracle(last.v, 32.152);
    }

    void steadyState150Flat() {
        Bicycle bike = makeBike();
        double distKm = 0;
        const double expected[] = {13.373, 18.521, 23.382, 25.299, 26.523};
        const int ticks[] = {50, 100, 200, 300, 600};
        int done = 0;
        SpeedDistance last{0, 0};
        for (int i = 0; i < 5; i++) {
            last = drive(bike, 150, 0, 0, ticks[i] - done, distKm);
            done = ticks[i];
            checkSpeed(last.v, expected[i]);
        }
        checkDistM(distKm * 1000, 379.881);
        checkOracle(last.v, 26.603);
    }

    void steadyState250Climb() {
        Bicycle bike = makeBike();
        double distKm = 0;
        const double expected[] = {11.062, 12.326, 12.670, 12.690, 12.691};
        const int ticks[] = {50, 100, 200, 300, 600};
        int done = 0;
        SpeedDistance last{0, 0};
        for (int i = 0; i < 5; i++) {
            last = drive(bike, 250, 8, 0, ticks[i] - done, distKm);
            done = ticks[i];
            checkSpeed(last.v, expected[i]);
        }
        checkDistM(distKm * 1000, 203.692);
        checkOracle(last.v, 12.680);
    }

    void zeroWattsStaysStill() {
        Bicycle bike = makeBike();
        double distKm = 0;
        SpeedDistance last = drive(bike, 0, 0, 0, 20, distKm);
        QVERIFY(last.v == 0.0);
        QVERIFY(distKm == 0.0);
    }

    void gradientStep() {
        Bicycle bike = makeBike();
        double distKm = 0;
        // 20 s flat, then 8% at the same 250 W. Speeds at ticks
        // 200 (step), 250 (+5 s), 300 (+10 s), 400 (+20 s), 600.
        const double expected[] = {29.263, 19.468, 14.842, 12.833, 12.691};
        const int ticks[] = {200, 250, 300, 400, 600};
        int done = 0;
        SpeedDistance last{0, 0};
        for (int i = 0; i < 5; i++) {
            double slope = (done >= 200) ? 8.0 : 0.0;
            last = drive(bike, 250, slope, 0, ticks[i] - done, distKm);
            done = ticks[i];
            checkSpeed(last.v, expected[i]);
        }
        checkDistM(distKm * 1000, 283.709);
    }

    void coastDown() {
        Bicycle bike = makeBike();
        double distKm = 0;
        // 30 s at 250 W flat, then 0 W. Speeds at ticks 300 (step),
        // 350 (+5 s), 400 (+10 s), 500 (+20 s).
        const double expected[] = {31.160, 26.092, 22.222, 16.699};
        const int ticks[] = {300, 350, 400, 500};
        int done = 0;
        SpeedDistance last{0, 0};
        for (int i = 0; i < 4; i++) {
            double watts = (done >= 300) ? 0.0 : 250.0;
            last = drive(bike, watts, 0, 0, ticks[i] - done, distKm);
            done = ticks[i];
            checkSpeed(last.v, expected[i]);
        }
        checkDistM(distKm * 1000, 330.214);
    }

    void spikeFilterClampsToPrevPlus200() {
        // A: 200/1500/200/200. C: 200/400/200/200. The filter clamps
        // a jump of more than 800 W to previous + 200, so with
        // deterministic dt and no hypoxia the two sequences run
        // identical watts and must produce exactly equal speeds.
        // (Probe: A 0.054/1.116/2.053/2.712, C 0.054/1.115/2.052/
        // 2.711 — agreeing to ~0.06% in the GUI model, where the
        // hypoxia factor applies twice; exact here.)
        const double seqA[] = {200, 1500, 200, 200};
        const double seqC[] = {200, 400, 200, 200};
        const double expected[] = {0.054, 1.115, 2.052, 2.711};
        Bicycle bikeA = makeBike(), bikeC = makeBike();
        for (int i = 0; i < 4; i++) {
            BicycleSimState sa(seqA[i], 0, 0), sc(seqC[i], 0, 0);
            SpeedDistance ra = bikeA.SampleSpeed(sa, kDt);
            SpeedDistance rc = bikeC.SampleSpeed(sc, kDt);
            QVERIFY(ra.v == rc.v);
            QVERIFY(ra.d == rc.d);
            checkSpeed(rc.v, expected[i], 0.05);
        }
    }

    void explicitDtRules() {
        // First tick forces dt = 0.01 s whatever is passed.
        Bicycle a = makeBike(), b = makeBike();
        BicycleSimState sa(250, 0, 0), sb(250, 0, 0);
        SpeedDistance ra = a.SampleSpeed(sa, 5.0);
        SpeedDistance rb = b.SampleSpeed(sb, 0.01);
        QVERIFY(ra.v == rb.v);
        QVERIFY(ra.d == rb.d);

        // dt > 1.0 s clamps to 0.01 s.
        Bicycle c = makeBike(), d = makeBike();
        double dist = 0;
        drive(c, 250, 0, 0, 10, dist);
        dist = 0;
        drive(d, 250, 0, 0, 10, dist);
        BicycleSimState sc(250, 0, 0), sd(250, 0, 0);
        SpeedDistance rc = c.SampleSpeed(sc, 2.0);
        SpeedDistance rd = d.SampleSpeed(sd, 0.01);
        QVERIFY(rc.v == rd.v);
        QVERIFY(rc.d == rd.d);

        // dt <= 0 means no change: speed stays, distance is 0.
        Bicycle e = makeBike();
        double distE = 0;
        SpeedDistance before = drive(e, 250, 0, 0, 10, distE);
        BicycleSimState se(250, 0, 0);
        SpeedDistance re = e.SampleSpeed(se, 0.0);
        QVERIFY(re.v == before.v);
        QVERIFY(re.d == 0.0);
        SpeedDistance re2 = e.SampleSpeed(se, -1.0);
        QVERIFY(re2.v == before.v);
        QVERIFY(re2.d == 0.0);
    }
};

QTEST_MAIN(TestBicycleSim)
#include "testBicycleSim.moc"
