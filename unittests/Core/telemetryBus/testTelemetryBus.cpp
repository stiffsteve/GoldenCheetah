// Phase 2 tests: TelemetryBus fan-out and TelemetryMerge dropout.
//
// No sleep() anywhere: all times are explicit parameters to
// mergeSources(), and the bus is synchronous, so no timing is
// involved at all. The bus tests use a recording observer that
// copies each received sample into a list; the merge tests use
// per-source samples with explicit received_ms values.
//
// Naming in the merge tests: "power" is a power meter source
// (power_valid, no HR), "hr" is a chest strap source (hr_valid,
// no power). Their received_ms values are chosen per test, not
// shared across tests, so each test's freshness arithmetic is
// readable on its own.

#include "Core/TelemetryBus.h"
#include "Core/TelemetryMerge.h"

#include <QTest>
#include <vector>

namespace {

class RecordingObserver : public ITelemetryObserver {
public:
    std::vector<TelemetrySample> received;
    void onTelemetry(const TelemetrySample &sample) override {
        // Copy, then mutate the copy: this must not affect the
        // bus, latest(), or other observers (immutability test).
        TelemetrySample copy = sample;
        copy.watts = 9999;
        received.push_back(sample);
        (void)copy;
    }
};

class UnsubscribingObserver : public ITelemetryObserver {
public:
    TelemetryBus *bus = nullptr;
    int calls = 0;
    void onTelemetry(const TelemetrySample &) override {
        ++calls;
        // Unsubscribing during publish must not break iteration
        // (bus iterates over a copy of the observer list).
        bus->unsubscribe(this);
    }
};

TelemetrySample powerSample(double watts) {
    TelemetrySample sample;
    sample.watts = watts;
    sample.power_valid = true;
    sample.cadence = 90;
    return sample;
}

TelemetrySample hrSample(double hr) {
    TelemetrySample sample;
    sample.heartrate = hr;
    sample.hr_valid = true;
    sample.speed_kph = 30;
    return sample;
}

} // namespace

class TestTelemetryBus : public QObject {
    Q_OBJECT

private slots:
    // ------------------------------------------------------------ bus
    void noLatestBeforePublish() {
        TelemetryBus bus;
        QVERIFY(!bus.hasLatest());
        QCOMPARE(bus.observerCount(), size_t(0));
        TelemetrySample empty = bus.latest();
        QVERIFY(!empty.power_valid);
        QCOMPARE(empty.watts, 0.0);
    }

    void fanOutInOrder() {
        TelemetryBus bus;
        RecordingObserver first, second;
        bus.subscribe(&first);
        bus.subscribe(&second);
        QCOMPARE(bus.observerCount(), size_t(2));

        TelemetrySample sample;
        sample.watts = 200;
        sample.power_valid = true;
        bus.publish(sample);
        QCOMPARE(first.received.size(), size_t(1));
        QCOMPARE(second.received.size(), size_t(1));
        QCOMPARE(first.received[0].watts, 200.0);
        QCOMPARE(second.received[0].watts, 200.0);

        // Second publish: both observers receive it again, in order.
        sample.watts = 210;
        bus.publish(sample);
        QCOMPARE(first.received.size(), size_t(2));
        QCOMPARE(first.received[1].watts, 210.0);
        QVERIFY(bus.hasLatest());
        QCOMPARE(bus.latest().watts, 210.0);
    }

    void doubleSubscribeIsOneDelivery() {
        TelemetryBus bus;
        RecordingObserver observer;
        bus.subscribe(&observer);
        bus.subscribe(&observer);
        QCOMPARE(bus.observerCount(), size_t(1));
        TelemetrySample sample;
        bus.publish(sample);
        QCOMPARE(observer.received.size(), size_t(1));
    }

    void unsubscribeStopsDelivery() {
        TelemetryBus bus;
        RecordingObserver first, second;
        bus.subscribe(&first);
        bus.subscribe(&second);
        bus.unsubscribe(&first);
        QCOMPARE(bus.observerCount(), size_t(1));
        TelemetrySample sample;
        bus.publish(sample);
        QCOMPARE(first.received.size(), size_t(0));
        QCOMPARE(second.received.size(), size_t(1));
        // Unsubscribing a non-subscriber is a no-op, not a crash.
        bus.unsubscribe(&first);
        QCOMPARE(bus.observerCount(), size_t(1));
    }

    void publishWithNoObservers() {
        TelemetryBus bus;
        TelemetrySample sample;
        sample.watts = 180;
        sample.power_valid = true;
        bus.publish(sample); // no observers: latest still stored
        QVERIFY(bus.hasLatest());
        QCOMPARE(bus.latest().watts, 180.0);
    }

    void observerMutationDoesNotAffectBus() {
        // RecordingObserver copies each sample and mutates its
        // copy to 9999 W. If the bus passed a shared mutable
        // sample, latest() would show 9999; it must show the
        // published value instead.
        TelemetryBus bus;
        RecordingObserver observer;
        bus.subscribe(&observer);
        TelemetrySample sample;
        sample.watts = 200;
        sample.power_valid = true;
        bus.publish(sample);
        QCOMPARE(bus.latest().watts, 200.0);
        QCOMPARE(observer.received[0].watts, 200.0);
    }

    void unsubscribeDuringPublish() {
        TelemetryBus bus;
        UnsubscribingObserver unsubscriber;
        unsubscriber.bus = &bus;
        RecordingObserver other;
        bus.subscribe(&unsubscriber);
        bus.subscribe(&other);
        TelemetrySample sample;
        bus.publish(sample);
        QCOMPARE(unsubscriber.calls, 1);
        QCOMPARE(other.received.size(), size_t(1));
        QCOMPARE(bus.observerCount(), size_t(1));
        // Second publish: unsubscriber no longer receives.
        bus.publish(sample);
        QCOMPARE(unsubscriber.calls, 1);
        QCOMPARE(other.received.size(), size_t(2));
    }

    // ---------------------------------------------------------- merge
    void mergePowerAndHrBothSurvive() {
        std::vector<SourceSample> sources = {
            {"power", powerSample(250), 1000},
            {"hr", hrSample(150), 1000},
        };
        TelemetrySample merged = mergeSources(sources, 1500, 3000);
        QCOMPARE(merged.watts, 250.0);
        QVERIFY(merged.power_valid);
        QCOMPARE(merged.heartrate, 150.0);
        QVERIFY(merged.hr_valid);
        // Remaining fields come from the newest sample overall
        // (ties: later list entry wins, so the HR source's speed).
        QCOMPARE(merged.speed_kph, 30.0);
    }

    void mergeNewestPowerWins() {
        std::vector<SourceSample> sources = {
            {"power-old", powerSample(200), 1000},
            {"power-new", powerSample(250), 2000},
        };
        TelemetrySample merged = mergeSources(sources, 2500, 3000);
        QCOMPARE(merged.watts, 250.0);
        QVERIFY(merged.power_valid);
    }

    void mergePowerDropoutKeepsLastValueNotZero() {
        // Power at 1000 ms, HR at 4000 ms, now 5000 ms, timeout
        // 3000 ms: power is stale (4000 > 3000), HR is fresh.
        std::vector<SourceSample> sources = {
            {"power", powerSample(250), 1000},
            {"hr", hrSample(150), 4000},
        };
        TelemetrySample merged = mergeSources(sources, 5000, 3000);
        QVERIFY(!merged.power_valid);
        QCOMPARE(merged.watts, 250.0); // last known, NOT zero
        QVERIFY(merged.hr_valid);
        QCOMPARE(merged.heartrate, 150.0);
    }

    void mergeHrDropoutIndependentOfPower() {
        // HR at 1000 ms, power at 4000 ms, now 5000 ms: HR stale,
        // power fresh. Power dropout must not invalidate HR's
        // counterpart, and vice versa.
        std::vector<SourceSample> sources = {
            {"hr", hrSample(150), 1000},
            {"power", powerSample(250), 4000},
        };
        TelemetrySample merged = mergeSources(sources, 5000, 3000);
        QVERIFY(!merged.hr_valid);
        QCOMPARE(merged.heartrate, 150.0); // last known, NOT zero
        QVERIFY(merged.power_valid);
        QCOMPARE(merged.watts, 250.0);
    }

    void mergeAllStaleIsDropoutSample() {
        std::vector<SourceSample> sources = {
            {"power", powerSample(250), 1000},
            {"hr", hrSample(150), 1000},
        };
        TelemetrySample merged = mergeSources(sources, 9000, 3000);
        QVERIFY(!merged.power_valid);
        QVERIFY(!merged.hr_valid);
        QCOMPARE(merged.watts, 250.0);
        QCOMPARE(merged.heartrate, 150.0);
    }

    void mergeTimeoutBoundaryIsFresh() {
        std::vector<SourceSample> sources = {
            {"power", powerSample(250), 1000},
            {"hr", hrSample(150), 1000},
        };
        // Exactly at the timeout (4000 - 1000 == 3000) is fresh;
        // one millisecond later it is stale.
        TelemetrySample fresh = mergeSources(sources, 4000, 3000);
        QVERIFY(fresh.power_valid);
        QVERIFY(fresh.hr_valid);
        TelemetrySample stale = mergeSources(sources, 4001, 3000);
        QVERIFY(!stale.power_valid);
        QVERIFY(!stale.hr_valid);
    }

    void mergeEmptyInput() {
        TelemetrySample merged = mergeSources({}, 0, 3000);
        QVERIFY(!merged.power_valid);
        QVERIFY(!merged.hr_valid);
        QCOMPARE(merged.watts, 0.0);
        QCOMPARE(merged.heartrate, 0.0);
    }

    void mergeDoesNotComputeWatts3s() {
        TelemetrySample sample = powerSample(250);
        sample.watts_3s = 0; // a device must not set this; merge must not compute it
        std::vector<SourceSample> sources = {{"power", sample, 1000}};
        TelemetrySample merged = mergeSources(sources, 1500, 3000);
        QCOMPARE(merged.watts_3s, 0.0);
        QVERIFY(merged.power_valid);
    }
};

QTEST_MAIN(TestTelemetryBus)
#include "testTelemetryBus.moc"
