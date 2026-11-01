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

// Phase 4: live derived metrics (core/view split, TDD Q8 and
// section 7.7 step 4: "Compute live derived metrics in core:
// 3s power, normalized power, W'bal, TSS so far").
//
// These are pure functions: a history of samples plus an
// athlete profile in, metric values out. No widgets, no
// session, no clock. TrainSession calls them on each tick;
// unit tests call them directly with hand-built histories.
// That separation is deliberate: a metric that can only be
// tested through a full session is a metric that cannot be
// trusted.
//
// History entry: one recorded (1 Hz) or tick sample, with
// its monotonic time and its watts, plus whether the watts
// were valid. Invalid (dropout) samples contribute nothing
// to averages — a dropout is not zero watts, here as in
// merging (Phase 2).
//
// Provisional choices, stated so they can be challenged:
//  - W'bal uses FTP as the critical-power proxy, because
//    AthleteProfile carries no CP field, and a W' capacity
//    of 20,000 J, the standard default. These are not
//    athlete-specific values; a future profile extension can
//    replace them without changing the function signatures
//    (the capacity and CP are parameters, not globals).
//  - Normalized power uses the standard Coggan definition:
//    30-second rolling average, raised to the fourth power,
//    averaged, then the fourth root. With less than 30 s of
//    history, the rolling window is whatever history exists
//    so far; NP is still defined, it is just noisier early.
//  - TSS so far uses the standard formula: duration in hours
//    times normalized power times intensity factor (NP/FTP),
//    divided by FTP, times 100. With no history, TSS is 0.
//  - All windows are by monotonic time, not by sample count,
//    so the functions are correct at any tick cadence (250 ms
//    ticks and 1 Hz recording give the same windows).
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_LiveMetrics_h
#define _GC_LiveMetrics_h 1

#include "AthleteProfile.h"

#include <cmath>
#include <cstdint>
#include <vector>

struct PowerHistoryEntry {
    int64_t monotonic_ms = 0;
    double watts = 0;
    bool power_valid = false;
};

struct LiveMetrics {
    double watts_3s = 0;
    double normalized_power = 0;
    double wbal_joules = 0;
    double tss_so_far = 0;
};

// Mean of valid watts in (now_ms - window_ms, now_ms].
// Empty window (no valid samples) gives 0, not a stale value.
inline double meanWattsInWindow(
    const std::vector<PowerHistoryEntry> &history,
    int64_t now_ms, int64_t window_ms) {
    double sum = 0;
    size_t count = 0;
    for (const auto &entry : history) {
        if (!entry.power_valid) {
            continue;
        }
        if (entry.monotonic_ms > now_ms - window_ms &&
            entry.monotonic_ms <= now_ms) {
            sum += entry.watts;
            ++count;
        }
    }
    return count == 0 ? 0 : sum / static_cast<double>(count);
}

inline double watts3s(
    const std::vector<PowerHistoryEntry> &history,
    int64_t now_ms) {
    return meanWattsInWindow(history, now_ms, 3000);
}

// Coggan normalized power: for each valid sample, the mean
// of valid watts in the preceding 30 s (including itself),
// raised to the fourth power; average those fourth powers;
// take the fourth root. Empty history gives 0.
inline double normalizedPower(
    const std::vector<PowerHistoryEntry> &history,
    int64_t now_ms) {
    double sum_fourth = 0;
    size_t count = 0;
    for (const auto &entry : history) {
        if (!entry.power_valid || entry.monotonic_ms > now_ms) {
            continue;
        }
        double rolling = meanWattsInWindow(history, entry.monotonic_ms, 30000);
        sum_fourth += std::pow(rolling, 4.0);
        ++count;
    }
    if (count == 0) {
        return 0;
    }
    return std::pow(sum_fourth / static_cast<double>(count), 0.25);
}

// Skiba W'bal differential model, stepped in history order.
// Above CP: W'bal decreases by (P - CP) * dt. At or below CP:
// W'bal recharges toward capacity with tau =
// 546 * exp(-0.01 * (CP - P)) + 316 seconds (Skiba's formula).
// Starts at capacity; clamped to [0, capacity]. Invalid
// samples are skipped (they consume no W' and recharge none,
// because no power was measured). CP and capacity are
// parameters, so the FTP proxy and 20,000 J default live in
// the caller (TrainSession), not in this function.
inline double wbal(
    const std::vector<PowerHistoryEntry> &history,
    double critical_power_watts, double w_prime_joules) {
    double balance = w_prime_joules;
    int64_t prev_ms = -1;
    for (const auto &entry : history) {
        if (!entry.power_valid) {
            continue;
        }
        if (prev_ms >= 0) {
            double dt_secs =
                static_cast<double>(entry.monotonic_ms - prev_ms) / 1000.0;
            if (dt_secs > 0) {
                if (entry.watts > critical_power_watts) {
                    balance -= (entry.watts - critical_power_watts) * dt_secs;
                } else {
                    double tau_secs =
                        546.0 * std::exp(-0.01 * (critical_power_watts -
                                                 entry.watts)) +
                        316.0;
                    balance += (w_prime_joules - balance) *
                               (1.0 - std::exp(-dt_secs / tau_secs));
                }
                if (balance < 0) {
                    balance = 0;
                }
                if (balance > w_prime_joules) {
                    balance = w_prime_joules;
                }
            }
        }
        prev_ms = entry.monotonic_ms;
    }
    return balance;
}

// TSS so far: duration_hours * NP * (NP / FTP) / FTP * 100.
// Duration is from the first valid sample to now_ms (not
// from session start, so a paused gap with no samples does
// not inflate TSS). FTP of 0 gives 0, not a division by zero.
inline double tssSoFar(
    const std::vector<PowerHistoryEntry> &history,
    int64_t now_ms, double ftp_watts) {
    if (ftp_watts <= 0) {
        return 0;
    }
    int64_t first_ms = -1;
    for (const auto &entry : history) {
        if (entry.power_valid) {
            first_ms = entry.monotonic_ms;
            break;
        }
    }
    if (first_ms < 0) {
        return 0;
    }
    double duration_hours =
        static_cast<double>(now_ms - first_ms) / 3600000.0;
    double np = normalizedPower(history, now_ms);
    double intensity_factor = np / ftp_watts;
    return duration_hours * np * intensity_factor / ftp_watts * 100.0;
}

// All four metrics at once, for a session tick. The caller
// passes the history up to and including the current sample.
inline LiveMetrics computeLiveMetrics(
    const std::vector<PowerHistoryEntry> &history,
    int64_t now_ms, const AthleteProfile &profile) {
    LiveMetrics metrics;
    metrics.watts_3s = watts3s(history, now_ms);
    metrics.normalized_power = normalizedPower(history, now_ms);
    // FTP as CP proxy, 20,000 J W' capacity: see the header
    // comment. These are the caller's defaults, not hidden
    // constants in wbal() itself.
    metrics.wbal_joules = wbal(history, profile.ftp_watts, 20000.0);
    metrics.tss_so_far = tssSoFar(history, now_ms, profile.ftp_watts);
    return metrics;
}

#endif // _GC_LiveMetrics_h
