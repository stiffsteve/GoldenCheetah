// Bt40DeviceAdapter tests — headless, no radio, no sleeps.
//
// Drives Bt40DeviceAdapter against a stub IBt40Backend: the
// adapter's connect/start/stop gating, command translation,
// sample mapping, distance accumulation, disconnect behaviour,
// calibration forwarding, and queue-and-drain ordering
// (samples only ever come out of tick()).
//
// The stub records every backend call so the translation can
// be asserted exactly at the adapter/backend boundary; the
// production backend (Bt40Backend.cpp) is NOT linked here, so
// no Bluetooth stack, no BT40Controller, no event loop.
//
// No QMessageBox / toast leakage by construction: this suite
// links only testlib + core (see bt40Adapter.pro — no widgets,
// no bluetooth). The adapter header is Qt-free and the adapter
// .cpp includes no GUI header; if it ever did, this binary
// would fail to link.

#include "Train/Bt40DeviceAdapter.h"

#include "Core/CalibrationData.h"

#include <QTest>

#include <memory>
#include <vector>

namespace {

// Stub backend: deterministic, records every call.
class StubBackend : public IBt40Backend {
public:
    bool connected = false;
    bool connectResult = true;
    Bt40Telemetry telemetryValue;

    int connectCalls = 0;
    int disconnectCalls = 0;
    int setErgCalls = 0;
    double lastErgWatts = 0;
    int setSlopeCalls = 0;
    double lastSlopeGradient = 0;
    double lastSlopeWeight = 0;
    int setSimCalls = 0;
    double lastSimGradient = 0;
    int setResistanceCalls = 0;
    double lastResistanceLevel = 0;
    int requestCalibrationCalls = 0;
    int resetCalibrationCalls = 0;

    uint8_t calibrationTypeValue = CALIBRATION_TYPE_SPINDOWN;
    uint8_t calibrationStateValue = CALIBRATION_STATE_STARTED;
    double calibrationTargetSpeedValue = 30.0;
    uint16_t calibrationSpindownTimeValue = 1234;
    uint16_t calibrationZeroOffsetValue = 56;
    uint16_t calibrationSlopeValue = 78;

    bool connect() override
    {
        connectCalls++;
        connected = connectResult;
        return connected;
    }
    void disconnect() override
    {
        disconnectCalls++;
        connected = false;
    }
    bool isConnected() const override { return connected; }
    Bt40Telemetry telemetry() override { return telemetryValue; }
    void setErg(double watts) override
    {
        setErgCalls++;
        lastErgWatts = watts;
    }
    void setSlope(double gradient_pct, double weight_kg) override
    {
        setSlopeCalls++;
        lastSlopeGradient = gradient_pct;
        lastSlopeWeight = weight_kg;
    }
    void setSim(double gradient_pct) override
    {
        setSimCalls++;
        lastSimGradient = gradient_pct;
    }
    void setResistance(double level_0_1) override
    {
        setResistanceCalls++;
        lastResistanceLevel = level_0_1;
    }
    void requestCalibration() override { requestCalibrationCalls++; }
    void resetCalibration() override { resetCalibrationCalls++; }
    uint8_t calibrationType() override { return calibrationTypeValue; }
    uint8_t calibrationState() override { return calibrationStateValue; }
    double calibrationTargetSpeed() override
    {
        return calibrationTargetSpeedValue;
    }
    uint16_t calibrationSpindownTime() override
    {
        return calibrationSpindownTimeValue;
    }
    uint16_t calibrationZeroOffset() override
    {
        return calibrationZeroOffsetValue;
    }
    uint16_t calibrationSlope() override
    {
        return calibrationSlopeValue;
    }
};

class RecordingSink : public ISampleSink {
public:
    std::vector<std::string> deviceIds;
    std::vector<TelemetrySample> samples;
    void onSample(const std::string &device_id,
                  const TelemetrySample &sample) override
    {
        deviceIds.push_back(device_id);
        samples.push_back(sample);
    }
};

DeviceInfo hubInfo()
{
    DeviceInfo info;
    info.id = "ble:AA:BB:CC:DD:EE:FF";
    info.name = "Zwift Hub";
    info.transport = "ble";
    info.is_controllable_trainer = true;
    info.has_power = true;
    info.has_speed_cadence = true;
    return info;
}

} // namespace

class TestBt40Adapter : public QObject {
    Q_OBJECT
private slots:
    void connectForwardsResult();
    void startBeforeConnectEmitsNothing();
    void tickBeforeStartEmitsNothing();
    void tickWithoutSinkDoesNotCrash();
    void tickEmitsMappedSample();
    void distanceAccumulatesFromSpeed();
    void zeroHeartrateIsInvalid();
    void disconnectStopsEmission();
    void stopResetsDistanceBaseline();
    void commandTranslation();
    void commandsIgnoredWhenDisconnected();
    void sampleCarriesLastCommand();
    void calibrationForwards();
    void samplesOnlyFromTick();

private:
    // Connected, started adapter with a sink, ready to tick.
    // The stub stays alive via the adapter (owned); keep a raw
    // pointer for assertions. (A void helper so QVERIFY can be
    // used: it bare-returns on failure and cannot live in a
    // function returning Fixture.)
    struct Fixture {
        std::unique_ptr<Bt40DeviceAdapter> adapter;
        StubBackend *stub = nullptr;
        RecordingSink sink;
    };
    void makeRunningAdapter(Fixture &f)
    {
        auto stub = std::make_unique<StubBackend>();
        f.stub = stub.get();
        f.adapter = std::make_unique<Bt40DeviceAdapter>(
            hubInfo(), std::move(stub));
        QVERIFY(f.adapter->connect());
        f.adapter->setSampleSink(&f.sink);
        f.adapter->start();
    }
};

void TestBt40Adapter::connectForwardsResult()
{
    auto stub = std::make_unique<StubBackend>();
    StubBackend *raw = stub.get();
    Bt40DeviceAdapter adapter(hubInfo(), std::move(stub));

    QVERIFY(adapter.connect());
    QVERIFY(adapter.isConnected());

    raw->connectResult = false;
    raw->connected = false;
    QVERIFY(!adapter.connect());
    QVERIFY(!adapter.isConnected());
    QCOMPARE(raw->connectCalls, 2);
}

void TestBt40Adapter::startBeforeConnectEmitsNothing()
{
    auto stub = std::make_unique<StubBackend>();
    StubBackend *raw = stub.get();
    Bt40DeviceAdapter adapter(hubInfo(), std::move(stub));
    RecordingSink sink;
    adapter.setSampleSink(&sink);
    adapter.start(); // started, but never connected
    raw->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    adapter.tick(1000);
    QVERIFY(sink.samples.empty());
}

void TestBt40Adapter::tickBeforeStartEmitsNothing()
{
    auto stub = std::make_unique<StubBackend>();
    Bt40DeviceAdapter adapter(hubInfo(), std::move(stub));
    RecordingSink sink;
    QVERIFY(adapter.connect());
    adapter.setSampleSink(&sink);
    // no start()
    adapter.tick(1000);
    QVERIFY(sink.samples.empty());
}

void TestBt40Adapter::tickWithoutSinkDoesNotCrash()
{
    auto stub = std::make_unique<StubBackend>();
    Bt40DeviceAdapter adapter(hubInfo(), std::move(stub));
    QVERIFY(adapter.connect());
    adapter.start();
    adapter.tick(1000); // no sink set: must be a no-op, not a crash
}

void TestBt40Adapter::tickEmitsMappedSample()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.size(), size_t(1));
    QCOMPARE(f.sink.deviceIds[0], std::string("ble:AA:BB:CC:DD:EE:FF"));
    const TelemetrySample &s = f.sink.samples[0];
    QCOMPARE(s.monotonic_ms, int64_t(1000));
    QCOMPARE(s.watts, 200.0);
    QCOMPARE(s.speed_kph, 36.0);
    QCOMPARE(s.cadence, 90.0);
    QCOMPARE(s.heartrate, 150.0);
    QCOMPARE(s.distance_m, 0.0); // first tick: no dt yet
    QCOMPARE(s.altitude_m, 0.0); // FTMS reports no altitude
    QVERIFY(s.power_valid);
    QVERIFY(s.hr_valid);
    QVERIFY(s.trainer_connected);
}

void TestBt40Adapter::distanceAccumulatesFromSpeed()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.back().distance_m, 0.0);
    f.adapter->tick(2000); // 36 kph = 10 m/s for 1 s
    QCOMPARE(f.sink.samples.back().distance_m, 10.0);
    f.adapter->tick(2500); // 0.5 s more
    QCOMPARE(f.sink.samples.back().distance_m, 15.0);
}

void TestBt40Adapter::zeroHeartrateIsInvalid()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 0};

    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.size(), size_t(1));
    QCOMPARE(f.sink.samples[0].heartrate, 0.0);
    QVERIFY(!f.sink.samples[0].hr_valid);
    QVERIFY(f.sink.samples[0].power_valid);
}

void TestBt40Adapter::disconnectStopsEmission()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.size(), size_t(1));

    f.stub->connected = false; // link dropped
    QVERIFY(!f.adapter->isConnected());
    f.adapter->tick(2000);
    f.adapter->tick(3000);
    QCOMPARE(f.sink.samples.size(), size_t(1)); // nothing more
}

void TestBt40Adapter::stopResetsDistanceBaseline()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    f.adapter->tick(1000);
    f.adapter->tick(2000);
    QCOMPARE(f.sink.samples.back().distance_m, 10.0);

    f.adapter->stop();
    f.adapter->tick(3000); // stopped: nothing
    QCOMPARE(f.sink.samples.size(), size_t(2));

    f.adapter->start();
    f.adapter->tick(4000); // first tick after restart: no dt
    QCOMPARE(f.sink.samples.size(), size_t(3));
    QCOMPARE(f.sink.samples.back().distance_m, 10.0);
    f.adapter->tick(5000);
    QCOMPARE(f.sink.samples.back().distance_m, 20.0);
}

void TestBt40Adapter::commandTranslation()
{
    Fixture f; makeRunningAdapter(f);

    f.adapter->setErgMode(250);
    QCOMPARE(f.stub->setErgCalls, 1);
    QCOMPARE(f.stub->lastErgWatts, 250.0);

    f.adapter->setSlopeMode(5, 80);
    QCOMPARE(f.stub->setSlopeCalls, 1);
    QCOMPARE(f.stub->lastSlopeGradient, 5.0);
    QCOMPARE(f.stub->lastSlopeWeight, 80.0);

    f.adapter->setSimMode(8, 0);
    QCOMPARE(f.stub->setSimCalls, 1);
    QCOMPARE(f.stub->lastSimGradient, 8.0);

    f.adapter->setResistanceMode(50);
    QCOMPARE(f.stub->setResistanceCalls, 1);
    QCOMPARE(f.stub->lastResistanceLevel, 0.5);

    // Resistance clamps to 0..100 before the /100 mapping.
    f.adapter->setResistanceMode(150);
    QCOMPARE(f.stub->lastResistanceLevel, 1.0);
    f.adapter->setResistanceMode(-10);
    QCOMPARE(f.stub->lastResistanceLevel, 0.0);
    QCOMPARE(f.stub->setResistanceCalls, 3);
}

void TestBt40Adapter::commandsIgnoredWhenDisconnected()
{
    auto stub = std::make_unique<StubBackend>();
    StubBackend *raw = stub.get();
    Bt40DeviceAdapter adapter(hubInfo(), std::move(stub));
    // never connected
    adapter.setErgMode(250);
    adapter.setSlopeMode(5, 80);
    adapter.setSimMode(8, 0);
    adapter.setResistanceMode(50);
    QCOMPARE(raw->setErgCalls, 0);
    QCOMPARE(raw->setSlopeCalls, 0);
    QCOMPARE(raw->setSimCalls, 0);
    QCOMPARE(raw->setResistanceCalls, 0);
}

void TestBt40Adapter::sampleCarriesLastCommand()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    f.adapter->setErgMode(250);
    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.back().target_watts, 250.0);
    QCOMPARE(f.sink.samples.back().gradient_pct, 0.0);

    f.adapter->setSlopeMode(5, 80);
    f.adapter->tick(2000);
    QCOMPARE(f.sink.samples.back().gradient_pct, 5.0);
    QCOMPARE(f.sink.samples.back().target_watts, 0.0);
}

void TestBt40Adapter::calibrationForwards()
{
    Fixture f; makeRunningAdapter(f);

    QCOMPARE(f.adapter->calibrationType(),
             uint8_t(CALIBRATION_TYPE_SPINDOWN));
    QCOMPARE(f.adapter->calibrationState(),
             uint8_t(CALIBRATION_STATE_STARTED));
    QCOMPARE(f.adapter->calibrationTargetSpeed(), 30.0);
    QCOMPARE(f.adapter->calibrationSpindownTime(), uint16_t(1234));
    QCOMPARE(f.adapter->calibrationZeroOffset(), uint16_t(56));
    QCOMPARE(f.adapter->calibrationSlope(), uint16_t(78));

    f.adapter->requestCalibration();
    QCOMPARE(f.stub->requestCalibrationCalls, 1);
    f.adapter->resetCalibrationState();
    QCOMPARE(f.stub->resetCalibrationCalls, 1);
}

void TestBt40Adapter::samplesOnlyFromTick()
{
    Fixture f; makeRunningAdapter(f);
    f.stub->telemetryValue = Bt40Telemetry{200, 36, 90, 150};

    // Telemetry arrives in the stub, but nothing is emitted
    // until the session thread ticks (queue-and-drain).
    QVERIFY(f.sink.samples.empty());
    f.stub->telemetryValue = Bt40Telemetry{220, 36, 95, 150};
    QVERIFY(f.sink.samples.empty());

    f.adapter->tick(1000);
    QCOMPARE(f.sink.samples.size(), size_t(1));
    QCOMPARE(f.sink.samples[0].watts, 220.0); // latest, not stale

    f.stub->telemetryValue = Bt40Telemetry{230, 36, 95, 150};
    QCOMPARE(f.sink.samples.size(), size_t(1)); // still 1, no tick
    f.adapter->tick(2000);
    QCOMPARE(f.sink.samples.size(), size_t(2));
    QCOMPARE(f.sink.samples[1].watts, 230.0);
}

QTEST_MAIN(TestBt40Adapter)
#include "testBt40Adapter.moc"
