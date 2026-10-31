/*
 * Copyright (c) 2026 GoldenCheetah contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA  02110-1301  USA
 */

// Phase 2: TelemetryBus (core/view split, TDD section 14, Phase 2).
// The observer fan-out for immutable TelemetrySample values.
// Replaces, in core, the pattern that RealtimeData plus the Qt
// signal Context::telemetryUpdate implement in the GUI: a mutable
// bag, passed by reference, that any consumer could in principle
// mutate and that every consumer had to understand in full.
//
// This bus does the opposite, on purpose:
//   - Samples are values, not shared mutable state. publish()
//     copies the sample into latest_ and passes it to observers
//     by const reference. An observer that copies the sample and
//     mutates its own copy cannot affect the bus, latest(), or
//     any other observer. That is tested, not just asserted.
//   - No threads, no Qt, no event loop. publish() is synchronous
//     and runs observers in subscription order, in the caller's
//     thread. A later TrainSession tick (Phase 4) can call
//     publish() from a worker thread, but this bus itself adds
//     no threading and no locking; if a future caller publishes
//     from multiple threads, that caller must serialize, and
//     that rule is documented here rather than silently assumed.
//   - No filtering, no merging, no derived metrics. Merging
//     per-source samples is TelemetryMerge.h (Phase 2 step 2);
//     derived metrics are Phase 4. This bus fans out one merged
//     sample, whatever its producer decided it was.
//
// Observer interface: ITelemetryObserver has one method,
// onTelemetry(const TelemetrySample&). This is deliberately NOT
// the TDD 7.7 session observer, which also has
// onSessionStateChanged and onWorkoutStepChanged. Those are
// TrainSession concerns (Phase 4); a bus observer that only
// plots watts should not have to implement session lifecycle
// methods to receive samples. When TrainSession arrives, it can
// either adopt this interface for telemetry and add session
// notifications separately, or define its session observer on
// top of it — that choice is Phase 4's, not Phase 2's.
//
// Lifetime rules, pinned because the bus holds raw pointers:
//   - The bus does not own observers. An observer must call
//     unsubscribe() before it is destroyed; publishing to a
//     destroyed observer is undefined behaviour, exactly as
//     with a Qt signal/slot that was not disconnected.
//   - An observer must not delete itself or another observer
//     during publish(). It MAY unsubscribe itself (or another
//     observer) during publish(): publish() iterates over a copy
//     of the observer list, so unsubscribing mid-publish does
//     not break iteration, and the unsubscribed observer still
//     receives the sample already in flight (it was subscribed
//     when publish started). Subscribing during publish does
//     NOT deliver the in-flight sample to the new observer;
//     it will receive the next one.
//   - Subscribing the same observer twice is a no-op (it
//     receives each sample once). Unsubscribing an observer
//     that was never subscribed is a no-op.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other Phase 0/1/2 core types.

#ifndef _GC_TelemetryBus_h
#define _GC_TelemetryBus_h 1

#include "TelemetrySample.h"

#include <vector>

class ITelemetryObserver {
public:
    virtual ~ITelemetryObserver() = default;
    virtual void onTelemetry(const TelemetrySample &sample) = 0;
};

class TelemetryBus {
public:
    void subscribe(ITelemetryObserver *observer) {
        if (observer == nullptr) {
            return;
        }
        for (ITelemetryObserver *existing : observers_) {
            if (existing == observer) {
                return; // already subscribed: one delivery per sample
            }
        }
        observers_.push_back(observer);
    }

    void unsubscribe(ITelemetryObserver *observer) {
        for (auto it = observers_.begin(); it != observers_.end(); ++it) {
            if (*it == observer) {
                observers_.erase(it);
                return;
            }
        }
    }

    void publish(const TelemetrySample &sample) {
        latest_ = sample;
        has_latest_ = true;
        // Iterate over a copy: an observer may unsubscribe during
        // publish (see the lifetime rules above) without breaking
        // iteration. Observers are still called in subscription order.
        std::vector<ITelemetryObserver *> current = observers_;
        for (ITelemetryObserver *observer : current) {
            observer->onTelemetry(sample);
        }
    }

    bool hasLatest() const { return has_latest_; }

    // The last published sample, by value. Callers must check
    // hasLatest() first; before any publish, this returns a
    // default (all-zero, invalid) sample, not a crash.
    TelemetrySample latest() const { return latest_; }

    size_t observerCount() const { return observers_.size(); }

private:
    std::vector<ITelemetryObserver *> observers_;
    TelemetrySample latest_;
    bool has_latest_ = false;
};

#endif // _GC_TelemetryBus_h
