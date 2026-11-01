// Phase 3 tests: FakeDevice and DeviceManager.
//
// No sleep() anywhere: all time is an explicit tick() parameter,
// and all devices are manually ticked (TDD 15.4 — manual ticking
// is the feature, not a Qt timer). A recording sink collects
// samples with their device ids, so tests can assert both the
// values and that they came from the right device.

#include "Core/DeviceManager.h"
#include "Core/FakeDevice.h"

#include <QTest>
#include <vector>

namespace {

class RecordingSink : public ISampleSink {
public:
    std::vector<std::string> ids;
    std::vector<TelemetrySample> samples;
    void onSample(const std::string &device_id,
                  const TelemetrySample &sample) override {
        ids.push_back(device_id);
        samples.push_back(sample);
    }
};

TelemetrySample scriptSample(int64_t ms, double watts, double hr) {
    TelemetrySample sample;
    sample.monotonic_ms = ms;
    sample.watts = watts;
    sample.power_valid = true;
    sample.heartrate = hr;
    sample.hr_valid = true;
    sample.cadence = 90;
    return sample;
}

FakeDeviceConfig scriptConfig(const std::string &name) {
    FakeDeviceConfig config;
    config.name = name;
    config.script = {
        scriptSample(0, 100, 120),
        scriptSample(1000, 200, 130),
        scriptSample(2000, 300, 140),
    };
    return config;
}

} // namespace

class TestFakeDevice : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------ FakeDevice
    void infoAndConnect() {
        FakeDevice device(scriptConfig("test"));
        DeviceInfo info = device.info();
        QCOMPARE(info.id, std::string("fake:test"));
        QCOMPARE(info.name, std::string("test"));
        QCOMPARE(info.transport, std::string("virtual"));
        QVERIFY(info.is_controllable_trainer);
        QVERIFY(info.has_power);
        QVERIFY(!device.isConnected());
        QVERIFY(device.connect());
        QVERIFY(device.isConnected());
        device.disconnect();
        QVERIFY(!device.isConnected());
    }

    void connectFails() {
        FakeDeviceConfig config = scriptConfig("broken");
        config.connect_fails = true;
        FakeDevice device(config);
        QVERIFY(!device.connect());
        QVERIFY(!device.isConnected());
        // Start on a disconnected device must not emit.
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.start();
        device.tick(5000);
        QCOMPARE(sink.samples.size(), size_t(0));
    }

    void startStopGatesEmission() {
        FakeDevice device(scriptConfig("test"));
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        // Not started: no samples, even with ticks.
        device.tick(5000);
        QCOMPARE(sink.samples.size(), size_t(0));
        device.start();
        QVERIFY(device.isStarted());
        device.tick(2000);
        QCOMPARE(sink.samples.size(), size_t(3));
        // Stop: further ticks emit nothing.
        device.stop();
        device.tick(9000);
        QCOMPARE(sink.samples.size(), size_t(3));
        // Start again restarts the script from the beginning.
        device.start();
        device.tick(2000);
        QCOMPARE(sink.samples.size(), size_t(6));
        QCOMPARE(sink.samples[3].watts, 100.0);
    }

    void nullSinkDoesNotCrash() {
        FakeDevice device(scriptConfig("test"));
        device.connect();
        device.start();
        device.tick(5000); // no sink set: must not crash
        QCOMPARE(device.emittedCount(), size_t(0));
    }

    void scriptEmittedInOrder() {
        FakeDevice device(scriptConfig("test"));
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.tick(2000);
        QCOMPARE(sink.samples.size(), size_t(3));
        QCOMPARE(sink.ids[0], std::string("fake:test"));
        QCOMPARE(sink.samples[0].watts, 100.0);
        QCOMPARE(sink.samples[1].watts, 200.0);
        QCOMPARE(sink.samples[2].watts, 300.0);
        QCOMPARE(sink.samples[0].monotonic_ms, int64_t(0));
        QCOMPARE(sink.samples[2].monotonic_ms, int64_t(2000));
    }

    void lagDelaysEmission() {
        FakeDeviceConfig config = scriptConfig("laggy");
        config.lag_ms = 500;
        FakeDevice device(config);
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.tick(400); // first sample due at 0 + 500, not yet
        QCOMPARE(sink.samples.size(), size_t(0));
        device.tick(500); // exactly due
        QCOMPARE(sink.samples.size(), size_t(1));
        device.tick(1499); // second sample due at 1000 + 500
        QCOMPARE(sink.samples.size(), size_t(1));
        device.tick(1500);
        QCOMPARE(sink.samples.size(), size_t(2));
    }

    void dropoutSkipsSamples() {
        FakeDeviceConfig config = scriptConfig("drop");
        // Drop the middle script sample (time 1000 ms).
        config.dropouts = {{1000, 2000}};
        FakeDevice device(config);
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.tick(2000);
        // First and third samples only; the dropped one is not
        // emitted late after the dropout ends.
        QCOMPARE(sink.samples.size(), size_t(2));
        QCOMPARE(sink.samples[0].watts, 100.0);
        QCOMPARE(sink.samples[1].watts, 300.0);
    }

    void noiseIsDeterministicAndZeroIsExact() {
        FakeDeviceConfig exact = scriptConfig("exact");
        exact.noise_watts = 0;
        exact.noise_hr = 0;
        FakeDevice deviceExact(exact);
        RecordingSink sinkExact;
        deviceExact.setSampleSink(&sinkExact);
        deviceExact.connect();
        deviceExact.start();
        deviceExact.tick(2000);
        QCOMPARE(sinkExact.samples[1].watts, 200.0);
        QCOMPARE(sinkExact.samples[1].heartrate, 130.0);

        // Same seed, same ticks: same noisy output, twice.
        FakeDeviceConfig noisy = scriptConfig("noisy");
        noisy.noise_watts = 10;
        noisy.noise_hr = 5;
        noisy.noise_seed = 42;
        FakeDevice first(noisy), second(noisy);
        RecordingSink sinkFirst, sinkSecond;
        first.setSampleSink(&sinkFirst);
        second.setSampleSink(&sinkSecond);
        first.connect();
        second.connect();
        first.start();
        second.start();
        first.tick(2000);
        second.tick(2000);
        QCOMPARE(sinkFirst.samples.size(), sinkSecond.samples.size());
        for (size_t i = 0; i < sinkFirst.samples.size(); ++i) {
            QCOMPARE(sinkFirst.samples[i].watts,
                     sinkSecond.samples[i].watts);
            QCOMPARE(sinkFirst.samples[i].heartrate,
                     sinkSecond.samples[i].heartrate);
        }
        // Noise actually changes at least one value from exact.
        bool changed = false;
        for (size_t i = 0; i < sinkFirst.samples.size(); ++i) {
            if (sinkFirst.samples[i].watts != sinkExact.samples[i].watts) {
                changed = true;
            }
        }
        QVERIFY(changed);
    }

    void ergConvergesWithoutOvershoot() {
        FakeDeviceConfig config;
        config.name = "erg";
        config.erg_tau_ms = 1000;
        FakeDevice device(config);
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setErgMode(250);
        device.tick(0);
        device.tick(1000);
        // After 1 s at tau 1000 ms: 250 * (1 - exp(-1)) = 158 W.
        double after1s = sink.samples.back().watts;
        QVERIFY(after1s > 150.0 && after1s < 170.0);
        device.tick(5000);
        double after5s = sink.samples.back().watts;
        QVERIFY(after5s > 240.0 && after5s <= 250.0);
        QCOMPARE(device.lastErgTarget(), 250.0);
        QVERIFY(device.hasErgTarget());
        // A new target re-converges from the current value.
        device.setErgMode(100);
        device.tick(10000);
        double afterRetarget = sink.samples.back().watts;
        QVERIFY(afterRetarget < after5s);
        QVERIFY(afterRetarget > 100.0);
    }

    void ergOverridesScriptWatts() {
        FakeDevice device(scriptConfig("erg-script"));
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.tick(0); // first script sample, before any ERG target
        device.setErgMode(250);
        device.tick(1000); // converges toward 250, emits script sample 2
        device.tick(2000); // converges further, emits script sample 3
        // Script HR/cadence survive, but watts after the ERG
        // target is set are ERG-converged, not the script's
        // 200/300 W. The first sample is pre-ERG, so it is exact.
        QCOMPARE(sink.samples.size(), size_t(3));
        QCOMPARE(sink.samples[0].watts, 100.0);
        QCOMPARE(sink.samples[1].heartrate, 130.0);
        QCOMPARE(sink.samples[1].cadence, 90.0);
        QVERIFY(sink.samples[1].watts != 200.0);
        QVERIFY(sink.samples[2].watts != 300.0);
        QVERIFY(sink.samples[2].watts > sink.samples[1].watts);
    }

    void slopeAndResistanceCommandsRecorded() {
        FakeDevice device(scriptConfig("slope"));
        device.connect();
        device.start();
        device.setSlopeMode(5, 75);
        QCOMPARE(device.lastSlopeGradient(), 5.0);
        QCOMPARE(device.lastSlopeWeightKg(), 75.0);
        device.setResistanceMode(42);
        QVERIFY(device.hasResistance());
        QCOMPARE(device.lastResistanceLevel(), 42);
        // Clamping, not a crash.
        device.setResistanceMode(999);
        QCOMPARE(device.lastResistanceLevel(), 100);
        device.setResistanceMode(-5);
        QCOMPARE(device.lastResistanceLevel(), 0);
        device.setSimMode(3, 25);
        QCOMPARE(device.lastSimGradient(), 3.0);
        QCOMPARE(device.lastSimSpeedKph(), 25.0);
        // Control on a disconnected device is ignored.
        device.disconnect();
        device.setErgMode(999);
        QVERIFY(!device.hasErgTarget());
    }

    void slopeGeneratedSpeed() {
        FakeDeviceConfig config;
        config.name = "slope-gen";
        config.base_speed_kph = 30;
        FakeDevice device(config);
        RecordingSink sink;
        device.setSampleSink(&sink);
        device.connect();
        device.start();
        device.setSlopeMode(5, 75);
        device.tick(1000);
        QCOMPARE(sink.samples.size(), size_t(1));
        QCOMPARE(sink.samples[0].gradient_pct, 5.0);
        QCOMPARE(sink.samples[0].speed_kph, 25.0); // 30 - 5
    }

    // -------------------------------------------------- DeviceManager
    void managerScanDoesNotConnect() {
        DeviceManager manager;
        QCOMPARE(manager.scan().size(), size_t(0));
        manager.registerFake(scriptConfig("a"));
        manager.registerFake(scriptConfig("b"));
        std::vector<DeviceInfo> found = manager.scan();
        QCOMPARE(found.size(), size_t(2));
        QCOMPARE(found[0].id, std::string("fake:a"));
        QCOMPARE(found[1].id, std::string("fake:b"));
        QVERIFY(manager.hasDevice("fake:a"));
        QVERIFY(!manager.hasDevice("fake:missing"));
    }

    void managerOpenGivesIndependentDevices() {
        DeviceManager manager;
        manager.registerFake(scriptConfig("a"));
        std::unique_ptr<IDevice> first = manager.open("fake:a");
        std::unique_ptr<IDevice> second = manager.open("fake:a");
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        QVERIFY(first.get() != second.get());
        // Opening does not remove the device from discovery.
        QCOMPARE(manager.scan().size(), size_t(1));
        // Each opened device has independent runtime state.
        RecordingSink sinkFirst, sinkSecond;
        first->setSampleSink(&sinkFirst);
        second->setSampleSink(&sinkSecond);
        first->connect();
        second->connect();
        first->start();
        // tick() is FakeDevice-specific, not part of IDevice, so
        // drive the opened device through a downcast here.
        FakeDevice *firstFake = dynamic_cast<FakeDevice *>(first.get());
        QVERIFY(firstFake != nullptr);
        firstFake->tick(2000);
        QCOMPARE(sinkFirst.samples.size(), size_t(3));
        QCOMPARE(sinkSecond.samples.size(), size_t(0));
    }

    void managerOpenUnknownIsNullptr() {
        DeviceManager manager;
        manager.registerFake(scriptConfig("a"));
        QVERIFY(manager.open("fake:missing") == nullptr);
        QVERIFY(manager.open("") == nullptr);
    }

    void managerRegisterReplaces() {
        DeviceManager manager;
        manager.registerFake(scriptConfig("a"));
        FakeDeviceConfig replacement = scriptConfig("a");
        replacement.script = {scriptSample(0, 999, 120)};
        manager.registerFake(replacement);
        QCOMPARE(manager.scan().size(), size_t(1));
        std::unique_ptr<IDevice> device = manager.open("fake:a");
        RecordingSink sink;
        device->setSampleSink(&sink);
        device->connect();
        device->start();
        // FakeDevice is an ITrainer; tick() is FakeDevice-only,
        // so drive it through a downcast in this test.
        FakeDevice *fake = dynamic_cast<FakeDevice *>(device.get());
        QVERIFY(fake != nullptr);
        fake->tick(0);
        QCOMPARE(sink.samples.size(), size_t(1));
        QCOMPARE(sink.samples[0].watts, 999.0);
    }
};

QTEST_MAIN(TestFakeDevice)
#include "testFakeDevice.moc"
