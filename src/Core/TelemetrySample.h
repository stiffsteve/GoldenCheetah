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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

// Phase 0 core type (core/view split, TDD section 7.1).
// One merged telemetry sample. Sensors, virtual power, and simulated
// feeds all produce the same struct.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// This header must stay that way; see doc/core-view-split/phase0-inventory.md.
//
// Rules carried over from the TDD:
//  - Samples are immutable once emitted. Views may cache, plot, log,
//    or ignore a sample, but must not mutate it.
//  - monotonic_ms is for control (steady clock, never goes backward).
//    wallclock_ms is for recording to file.
//  - power_valid and hr_valid must be explicit. A dropout is not zero
//    watts, it is invalid data.
//  - Devices never set watts_3s. That is a session-level derived
//    metric, computed by the train engine, not by devices.

#ifndef _GC_TelemetrySample_h
#define _GC_TelemetrySample_h 1

#include <cstdint>

struct TelemetrySample {
    int64_t monotonic_ms = 0;  // steady clock, for control. Never goes backward.
    int64_t wallclock_ms = 0;  // wall clock, for recording to file.
    double watts = 0;
    double watts_3s = 0;       // computed by TrainSession, not by devices
    double heartrate = 0;
    double cadence = 0;
    double speed_kph = 0;
    double distance_m = 0;
    double altitude_m = 0;
    double gradient_pct = 0;   // what the trainer is currently simulating
    double target_watts = 0;   // what the workout asked for
    bool power_valid = false;
    bool hr_valid = false;
    bool trainer_connected = false;
    int trainer_load_pct = 0;
};

#endif // _GC_TelemetrySample_h
