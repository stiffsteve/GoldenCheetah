// Zwift-like game support, step 3: VirtualRider tests.
//
// No sleep() anywhere: all time is an explicit tick()
// parameter, and the device is manually ticked (TDD 15.4 —
// manual ticking is the feature). A recording sink collects
// samples so tests can assert the values the device reports.
//
// Speed pins come from the physics suite's values: 250 W on
// the flat at 83 kg (75 rider + 8 bike) is 32.0819 kph, and
// at 8% it is 12.4797 kph. The tolerance is 0.05 kph, which
// also catches a missing bike weight (75 kg alone would give
// 32.2131 kph on the flat).

#include "Core/PhysicsUtility.h"
#include "Core/VirtualRider.h"

#include <QTest>
#include <cmath>
#include <vector>

namespace {

class RecordingSink : public ISampleSink {
public:
    std::vector<TelemetrySample> samples;
    void onSample(const std::string &device_id,
                  const TelemetrySample &sample) override {
        (void)device_id;
        samples.push_back(sample);
    }
};

VirtualRiderConfig defaultConfig() {
    VirtualRiderConfig config;
    config.name = "rider";
    config.rider_weight_kg = 75;
    config.bike_weight_kg = 8;
    config.erg_tau_ms = 1000;
    return config;
}

void drive(VirtualRider &device, int64_t duration_ms) {
    for (int64_t t = 0; t < duration_ms; t += 250) {
        device.tick(t);
    }
}

} // namespace

class TestVirtualRider : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------- VirtualRider
    void infoAndConnect() {
        VirtualRider device(defaultConfig());
        DeviceInfo info = device.info();
        QCOMPARE(info.id, std::string("virtual:rider"));
        QCOMPARE(info.transport, std::string("virtual"));
        QVERIFY(info.is_controllable_trainer);
        QVERIFY(info.has_power);
        QVERIFY(!device.isConnected());
        QVERIFY(device.connect());
        QVERIFY(device.isConnected());
        device.disconnect();
        QVERIFY(!device.isConnected());
    }

    void silentWithNoCommand() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(200);
        drive(device, 2000);
        // No ERG/slope/sim command: the device is silent, as
        // in FakeDevice, so the session's stale detection
        // still works.
        QCOMPARE(device.emittedCount(), size_t(0));
        QVERIFY(sink.samples.empty());
    }

    void slopeSpeedFromPhysics() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(250);
        device.setSlopeMode(0, 75);
        drive(device, 2000);
        QVERIFY(!sink.samples.empty());
        // Flat at 250 W, 83 kg: the physics pin, and the
        // wiring check against the function itself.
        QVERIFY(std::abs(device.speedKph() - 32.0819) < 0.05);
        QVERIFY(std::abs(device.speedKph() -
                         computeInstantSpeed(83, 0, 0, 250)) < 0.001);
        // Same watts on an 8% climb: much slower.
        device.setSlopeMode(8, 75);
        drive(device, 2000);
        QVERIFY(std::abs(device.speedKph() - 12.4797) < 0.05);
        QVERIFY(device.speedKph() < 15.0);
    }

    void simSpeedFromPhysics() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(200);
        device.setSimMode(0, 0); // speed param ignored: computed
        drive(device, 2000);
        QVERIFY(std::abs(device.speedKph() - 29.5532) < 0.05);
        device.setSimMode(8, 0);
        drive(device, 2000);
        QVERIFY(std::abs(device.speedKph() - 10.1644) < 0.05);
    }

    void ergConvergesToTarget() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(100);
        device.setErgMode(250);
        drive(device, 5000);
        // tau 1000 ms: after 5 s the watts are within ~2 W
        // of target, never overshooting.
        QVERIFY(device.currentWatts() > 240.0);
        QVERIFY(device.currentWatts() <= 250.0);
        QVERIFY(device.hasErgTarget());
        QCOMPARE(device.lastErgTarget(), 250.0);
        // And the samples carry the converged watts.
        QVERIFY(!sink.samples.empty());
        QVERIFY(sink.samples.back().watts > 240.0);
    }

    void distanceAccumulates() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(250);
        device.setSlopeMode(0, 75);
        drive(device, 10000);
        // ~32.08 kph = 8.91 m/s; 40 ticks at 250 ms give 39
        // intervals of 250 ms = 9.75 s, so ~86.9 m.
        QVERIFY(device.distanceM() > 80.0);
        QVERIFY(device.distanceM() < 95.0);
        // The samples carry the distance too.
        QVERIFY(!sink.samples.empty());
        QVERIFY(std::abs(sink.samples.back().distance_m -
                         device.distanceM()) < 0.001);
    }

    void altitudeAccumulatesOnClimb() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(250);
        device.setSlopeMode(8, 75);
        drive(device, 10000);
        // ~12.48 kph for 9.75 s is ~33.8 m of road; altitude
        // is road * sin(atan(0.08)) = road * 0.0797, ~2.7 m.
        QVERIFY(device.altitudeM() > 2.0);
        QVERIFY(device.altitudeM() < 3.5);
        // On the flat the altitude stays put.
        VirtualRider flat(defaultConfig());
        flat.setSampleSink(&sink);
        flat.connect();
        flat.start();
        flat.setVirtualWatts(250);
        flat.setSlopeMode(0, 75);
        drive(flat, 5000);
        QCOMPARE(flat.altitudeM(), 0.0);
    }

    void zeroWattsZeroSpeed() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(0);
        device.setSlopeMode(0, 75);
        drive(device, 2000);
        QCOMPARE(device.speedKph(), 0.0);
        QCOMPARE(device.distanceM(), 0.0);
    }

    void startResetsDistance() {
        VirtualRider device(defaultConfig());
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setVirtualWatts(250);
        device.setSlopeMode(0, 75);
        drive(device, 5000);
        QVERIFY(device.distanceM() > 0);
        device.stop();
        device.start(); // new ride
        QCOMPARE(device.distanceM(), 0.0);
        QCOMPARE(device.altitudeM(), 0.0);
    }
};

QTEST_MAIN(TestVirtualRider)
#include "testVirtualRider.moc"
