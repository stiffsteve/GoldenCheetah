// Zwift-like game support, step 2: pin PhysicsUtility's
// computeInstantSpeed after the move from src/Train to
// src/Core.
//
// The values below were measured from the function before
// the move (a probe program against the Train location);
// the suite asserts the moved code produces the same
// numbers, so the move is verified behaviour-neutral. The
// function is the GUI train view's virtual-speed model
// (TrainSidebar calls it every telemetry update), so these
// are the numbers the GUI already rides on — not values
// invented for this suite.
//
// Rider + bike mass is 83 kg (75 kg rider, 8 kg bike) unless
// stated; altitude 0 m; the other parameters are the
// function's defaults (crr 0.004, Cm 1.0, Cd 1.0, A 0.5,
// T 293.15 K), which are also the defaults the GUI uses.

#include "Core/PhysicsUtility.h"

#include <QTest>
#include <cmath>

namespace {

// The probe printed to 4 decimals; allow 0.05 kph of
// platform floating-point noise around the pinned value.
void checkSpeed(double actual, double expected) {
    QVERIFY(std::abs(actual - expected) < 0.05);
}

} // namespace

class TestPhysicsUtility : public QObject {
    Q_OBJECT

private slots:
    void flat250W() {
        checkSpeed(computeInstantSpeed(83, 0, 0, 250), 32.0819);
    }

    void flat150W() {
        checkSpeed(computeInstantSpeed(83, 0, 0, 150), 26.5367);
    }

    void climb8Percent250W() {
        checkSpeed(computeInstantSpeed(83, 8, 0, 250), 12.4797);
    }

    void climb8Percent150W() {
        checkSpeed(computeInstantSpeed(83, 8, 0, 150), 7.7408);
    }

    void descent5Percent250W() {
        checkSpeed(computeInstantSpeed(83, -5, 0, 250), 48.4890);
    }

    void zeroWattsIsZeroSpeed() {
        QVERIFY(std::abs(computeInstantSpeed(83, 0, 0, 0)) < 0.001);
    }

    void heavierRiderSlowerOnFlat() {
        // Rolling resistance scales with mass; aero dominates,
        // so the difference is small but the order is fixed.
        double light = computeInstantSpeed(83, 0, 0, 250);
        double heavy = computeInstantSpeed(100, 0, 0, 250);
        checkSpeed(heavy, 31.8033);
        QVERIFY(heavy < light);
    }

    void altitudeFasterThinnerAir() {
        double sea_level = computeInstantSpeed(83, 0, 0, 250);
        double altitude = computeInstantSpeed(83, 0, 2000, 250);
        checkSpeed(altitude, 34.0995);
        QVERIFY(altitude > sea_level);
    }
};

QTEST_MAIN(TestPhysicsUtility)
#include "testPhysicsUtility.moc"
