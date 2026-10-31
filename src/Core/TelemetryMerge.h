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

// Phase 2 step 2: merge per-source telemetry samples, with explicit
// dropout handling (core/view split, TDD section 14, Phase 2).
//
// The TDD's TrainSession tick merges "latest samples per device
// into one TelemetrySample. If power comes from one device and
// HR from another, both survive." This header implements exactly
// that merge as a pure function, before TrainSession exists, so
// the rule is testable without devices, threads, or a session.
//
// Inputs: a list of SourceSample values, each with a source_id
// (a stable string identifying one device or feed — see the
// Phase 0 note that a stable device id derivation is deferred
// to Phase 2 device work; here the id is simply whatever the
// caller supplies, and merging does not invent one), a
// TelemetrySample, and received_ms (when this source was last
// heard from, in monotonic_ms units).
//
// Rules, pinned by the Phase 2 unit tests:
//  - Freshness: a source is fresh if now_ms - received_ms <=
//    stale_timeout_ms (exactly at the timeout is still fresh;
//    strictly greater is stale). The timeout is a parameter, not
//    a constant: for ~1 Hz devices, 3000 ms is three missed
//    samples, but choosing the production value is Phase 3/4
//    work, not a guess baked into this function.
//  - Base: the merged sample starts as the newest sample overall
//    (by received_ms, ties broken by list order — the later
//    entry wins), for all fields that are not power or HR. That
//    covers cadence, speed, distance, altitude, gradient, target,
//    trainer fields, and monotonic/wallclock, which come from
//    the newest feed as a whole.
//  - Power: the newest fresh sample with power_valid == true
//    supplies watts, and merged power_valid is true. If no fresh
//    power-valid source exists, merged power_valid is false and
//    watts keeps the newest power value seen from any
//    power-valid source, fresh or stale — the last known value,
//    never zero. If no source ever supplied a power-valid value,
//    watts keeps the base (newest) sample's watts with
//    power_valid false: that will be 0 for a source that never
//    reported power, and the last reported value for a source
//    that reported power without validity. Either way, dropout
//    is invalid data, not zero watts, and consumers must gap
//    on power_valid == false instead of plotting 0.
//  - Heart rate: identical to power, using hr_valid and
//    heartrate. Power dropout and HR dropout are independent:
//    a power meter dropping out does not invalidate HR from a
//    chest strap, and vice versa.
//  - Empty input: merging zero sources returns a default sample
//    (all zero, both validity flags false). It does not crash
//    and does not invent a value.
//  - watts_3s is never set by merging. It is a session-level
//    derived metric (Phase 4), not a device value, per the
//    TelemetrySample rules. If a source sample carries a
//    watts_3s value, merging passes the base sample's value
//    through unchanged (it does not compute one), and callers
//    must not treat a merged watts_3s as a computed metric.
//  - The function does not mutate its inputs and does not keep
//    state between calls. A caller that wants "latest per
//    source" tracking must maintain its own source list (one
//    entry per source_id, updated on each receipt) and pass it
//    in; this function merges whatever list it is given, with
//    duplicate source_ids treated as separate entries (the
//    newest wins per field, as above).
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other Phase 0/1/2 core types.

#ifndef _GC_TelemetryMerge_h
#define _GC_TelemetryMerge_h 1

#include "TelemetrySample.h"

#include <cstdint>
#include <string>
#include <vector>

struct SourceSample {
    std::string source_id;
    TelemetrySample sample;
    int64_t received_ms = 0;
};

inline TelemetrySample mergeSources(const std::vector<SourceSample> &sources,
                                    int64_t now_ms,
                                    int64_t stale_timeout_ms) {
    TelemetrySample merged;
    if (sources.empty()) {
        return merged;
    }

    // Newest sample overall, for the base fields. Ties: later
    // list entry wins, so a caller that appends newer samples
    // gets the newest value.
    const SourceSample *newest = &sources[0];
    for (const SourceSample &source : sources) {
        if (source.received_ms >= newest->received_ms) {
            newest = &source;
        }
    }
    merged = newest->sample;

    // Newest fresh power-valid source, and newest power value
    // from any source (for the dropout case).
    const SourceSample *fresh_power = nullptr;
    const SourceSample *any_power = nullptr;
    const SourceSample *fresh_hr = nullptr;
    const SourceSample *any_hr = nullptr;
    for (const SourceSample &source : sources) {
        bool fresh = (now_ms - source.received_ms) <= stale_timeout_ms;
        if (source.sample.power_valid) {
            if (any_power == nullptr ||
                source.received_ms >= any_power->received_ms) {
                any_power = &source;
            }
            if (fresh && (fresh_power == nullptr ||
                          source.received_ms >= fresh_power->received_ms)) {
                fresh_power = &source;
            }
        }
        if (source.sample.hr_valid) {
            if (any_hr == nullptr ||
                source.received_ms >= any_hr->received_ms) {
                any_hr = &source;
            }
            if (fresh && (fresh_hr == nullptr ||
                          source.received_ms >= fresh_hr->received_ms)) {
                fresh_hr = &source;
            }
        }
    }

    if (fresh_power != nullptr) {
        merged.watts = fresh_power->sample.watts;
        merged.power_valid = true;
    } else if (any_power != nullptr) {
        merged.watts = any_power->sample.watts;
        merged.power_valid = false;
    } else {
        // No power-valid value ever: keep the base sample's
        // watts (0 if never reported) with power_valid false.
        // Never invent a zero over a reported value.
        merged.power_valid = false;
    }

    if (fresh_hr != nullptr) {
        merged.heartrate = fresh_hr->sample.heartrate;
        merged.hr_valid = true;
    } else if (any_hr != nullptr) {
        merged.heartrate = any_hr->sample.heartrate;
        merged.hr_valid = false;
    } else {
        merged.hr_valid = false;
    }

    return merged;
}

#endif // _GC_TelemetryMerge_h
