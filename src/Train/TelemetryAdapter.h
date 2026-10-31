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

// Phase 0 step 6: thin adapter at the Context telemetry boundary.
// Converts a legacy RealtimeData object to the Phase 0 TelemetrySample
// via src/Core/Mapping.h, without changing what the GUI renders.
//
// This header lives in Train, not Core, on purpose. Core must stay
// pure: RealtimeData.h pulls in GoldenCheetah.h and <QApplication>,
// so a mapping that takes RealtimeData cannot live in src/Core.
// The boundary pattern from Mapping.h is: Train code (where the old
// types live) fills a TelemetrySource and calls telemetryFromSource.
// This adapter is exactly that boundary code, factored out so
// Context::notifyTelemetryUpdate stays one line.
//
// This adapter is NOT checked by util/check-core-includes.sh. That
// script checks only the pure Phase 0 headers in src/Core; this
// file is boundary code and is allowed — indeed required — to touch
// the old types.
//
// PROVISIONAL DEFAULTS, READ BEFORE CONSUMING THIS SAMPLE
// -------------------------------------------------------
// v3.8 has no validity flags and no workout target in RealtimeData,
// so three fields cannot be derived faithfully here yet:
//  - power_valid / hr_valid: set true, preserving v3.8 semantics
//    (every value is treated as valid, dropout shows as 0). This is
//    NOT dropout detection. Do not use these flags to filter data
//    until Phase 1/4 adds validity tracking at the device boundary.
//    Setting them false instead would discard all live data.
//  - target_watts: set 0. The workout target lives in ErgFile, not
//    in RealtimeData; the Phase 1 session must fill it in.
//  - trainer_connected: RealtimeData has no "connected" flag. The
//    proxy used here is getTrainerStatusAvailable() (the trainer
//    reports status). If the trainer reports no status, the sample
//    is still produced, with trainer_connected false.
// SessionState is not produced here either: it needs lifecycle
// context (Running vs Paused) that a single RealtimeData does not
// carry. That mapping is deferred to the Phase 1 TrainSession,
// which owns lifecycle. See the inventory exit section.
//
// OUT OF SCOPE FOR THE PHASE 0 SAMPLE (DECIDED, SEE INVENTORY)
// ------------------------------------------------------------
// TelemetrySample carries 14 fields. The remaining RealtimeData
// series are out of scope for Phase 0 and are deliberately not
// mapped: altWatts, altDistance, virtualSpeed, wheelRpm, wbal,
// joules, xPower, bikeScore, RI, skibaVI, isoPower, bikeStress,
// IF, VI, lap fields, LRBalance, LTE/RTE, LPS/RPS, pedal metrics,
// smo2/thb, VO2/VCO2 and related, position, temp/coreTemp/
// skinTemp/heatStrain/heatLoad, latitude/longitude, spinScan,
// and all averages. They are derived or advanced metrics, not
// raw telemetry, and belong to Phase 4 live metrics or to the
// existing derived-data code, not to this sample. Adding them
// later means extending TelemetrySample, not changing this adapter.

#ifndef _GC_TelemetryAdapter_h
#define _GC_TelemetryAdapter_h 1

#include "RealtimeData.h"
#include "Core/Mapping.h"

#include <cstdint>

inline TelemetrySample telemetrySampleFromRealtimeData(
    const RealtimeData &rt, int64_t wallclock_ms) {
    TelemetrySource source;
    source.monotonic_ms = rt.getMsecs();
    source.watts = rt.getWatts();
    source.heartrate = rt.getHr();
    source.cadence = rt.getCadence();
    source.speed_kph = rt.getSpeed();
    source.distance_m = rt.getDistance();
    source.altitude_m = rt.getAltitude();
    source.gradient_pct = rt.getSlope();
    source.trainer_load_pct = rt.getLoad();
    // Provisional, see the header comment: v3.8 has no validity
    // flags, and no target in RealtimeData.
    source.target_watts = 0;
    source.power_valid = true;
    source.hr_valid = true;
    source.trainer_connected = rt.getTrainerStatusAvailable();
    return telemetryFromSource(source, wallclock_ms);
}

#endif // _GC_TelemetryAdapter_h
