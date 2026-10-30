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

// Phase 0 mapping functions (core/view split, TDD sections 7 and 14).
// The mapping tables from doc/core-view-split/phase0-inventory.md
// section 2, as code. Promoted from the test-local prototypes in
// unittests/Core/phase0Mapping/ in Phase 0 step 5.
//
// BOUNDARY PATTERN — READ THIS BEFORE "SIMPLIFYING" IT
// -----------------------------------------------------
// These functions deliberately do NOT take RealtimeData, ErgFile, or
// ZoneRange. Those headers pull in Qt Widgets, Context, Settings,
// and the GUI (see the test header comment in testPhase0Mapping.cpp
// for the full list), and including them here would drag the God
// Context into core. Instead, each mapping takes a plain source
// struct defined here, holding only the field values the mapping
// needs. The caller at the boundary — Train/Gui code where the old
// types already live, e.g. the step 6 Context::telemetryUpdate
// adapter — fills a source struct from the old object and calls
// this function. Core stays pure; the adapter stays thin. The
// source structs are not a second copy of the old types: they are
// the boundary contract, and they are what makes this header
// testable without linking the GUI.
//
// Rules carried over from the TDD and pinned by the mapping tests:
//  - Validity is explicit. A dropout is invalid data, not zero watts.
//  - Devices never set watts_3s. It is left 0 here; the session
//    computes it (TDD 7.1).
//  - WorkoutStep precedence is preserved, not resolved, here:
//    target_watts takes precedence if > 0, otherwise target_pct_ftp
//    is used. Resolution is WorkoutPlayer's job (Phase 1).
//  - AthleteSource.ftp_watts comes from a ZoneRange the caller
//    picks explicitly (FTP is per range/sport in v3.8, not a single
//    number); weight/bike/height come from athlete settings. The
//    choice of range/sport lives at the boundary, not here.
//
// TWO CORRECTIONS TO THE INVENTORY PLAN, RECORDED HERE
// ----------------------------------------------------
// 1. ErgFileSection, not ErgFilePoint, maps to WorkoutStep.
//    ErgFilePoint is a point (x/y/val) inside a section; a section
//    is the unit that becomes a step. Points become within-section
//    samples, handled by the Phase 1 parser, not by this mapping.
// 2. SessionState is NOT mapped here, on purpose. Trainer booleans
//    alone underdetermine it: Running vs Paused depends on session
//    lifecycle, and whether a brake fault / calibration / config
//    requirement means Error, PausedWithError, or Ready depends on
//    whether a session was running when it appeared. Inventing a
//    pure function of booleans would silently fix that decision.
//    The SessionState mapping belongs in the step 6 adapter, where
//    lifecycle context exists, and is recorded there as a deferred
//    sub-decision. The booleans in question are RealtimeData's
//    TrainerReady / Running / CalibRequired / ConfigRequired /
//    BrakeFault, plus RealtimeController start/restart/pause/stop
//    int return codes.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// This header is header-only, like the other Phase 0 types.

#ifndef _GC_Mapping_h
#define _GC_Mapping_h 1

#include "TelemetrySample.h"
#include "Workout.h"
#include "AthleteProfile.h"

#include <cstdint>
#include <string>

// Source values for a telemetry sample, as read from a device or a
// legacy RealtimeData object at the boundary. Field names and units
// follow the old types; see the inventory for the full old shapes.
struct TelemetrySource {
    int64_t monotonic_ms = 0; // RealtimeData time, in milliseconds
    double watts = 0;
    double heartrate = 0;
    double cadence = 0;
    double speed_kph = 0;
    double distance_m = 0;
    double altitude_m = 0;
    double gradient_pct = 0;  // RealtimeData slope
    double target_watts = 0;  // what the workout asked for
    double trainer_load_pct = 0; // RealtimeData load (double there)
    bool power_valid = false;   // source present, not "watts != 0"
    bool hr_valid = false;
    bool trainer_connected = false;
};

// Source values for one workout step, as read from a legacy
// ErgFileSection at the boundary. start_value is watts in ERG mode
// and gradient in Slope mode.
struct WorkoutSectionSource {
    int duration_ms = 0; // ErgFileSection duration
    double start_value = 0;
    bool is_slope = false;
    std::string label;
};

// Source values for an athlete profile, as read from a legacy
// ZoneRange (ftp) plus athlete settings (weights, height) at the
// boundary. See the AthleteSource note in the header comment.
struct AthleteSource {
    double ftp_watts = 0;
    double weight_kg = 75;
    double bike_weight_kg = 8;
    double height_cm = 175;
};

inline TelemetrySample telemetryFromSource(const TelemetrySource &source,
                                           int64_t wallclock_ms) {
    TelemetrySample sample;
    sample.monotonic_ms = source.monotonic_ms;
    sample.wallclock_ms = wallclock_ms;
    sample.watts = source.watts;
    // Devices never set watts_3s; the session computes it.
    sample.watts_3s = 0;
    sample.heartrate = source.heartrate;
    sample.cadence = source.cadence;
    sample.speed_kph = source.speed_kph;
    sample.distance_m = source.distance_m;
    sample.altitude_m = source.altitude_m;
    sample.gradient_pct = source.gradient_pct;
    sample.target_watts = source.target_watts;
    sample.power_valid = source.power_valid;
    sample.hr_valid = source.hr_valid;
    sample.trainer_connected = source.trainer_connected;
    // RealtimeData load is a double; TelemetrySample stores an int
    // percent. Truncation, not rounding, is the v3.8 load display
    // behaviour; keep it here so the mapping is exact, not prettied.
    sample.trainer_load_pct = static_cast<int>(source.trainer_load_pct);
    return sample;
}

inline WorkoutStep workoutStepFromSection(const WorkoutSectionSource &section) {
    WorkoutStep step;
    step.duration_secs = section.duration_ms / 1000.0;
    if (section.is_slope) {
        step.target_gradient_pct = section.start_value;
    } else {
        step.target_watts = section.start_value;
    }
    step.label = section.label;
    return step;
}

inline AthleteProfile athleteProfileFromSource(const AthleteSource &source) {
    AthleteProfile profile;
    profile.ftp_watts = source.ftp_watts;
    profile.weight_kg = source.weight_kg;
    profile.bike_weight_kg = source.bike_weight_kg;
    profile.height_cm = source.height_cm;
    return profile;
}

#endif // _GC_Mapping_h
