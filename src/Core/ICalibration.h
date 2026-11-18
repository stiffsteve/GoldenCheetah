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

// ICalibration — core extension (core/view split, TDD Q10).
// Calibration is a device operation, not a view operation: the
// device owns the spin-down state machine, the view only polls
// it and requests it.
//
// This interface mirrors RealtimeController's calibration
// virtuals in pure types:
//   getCalibrationType / State / TargetSpeed / SpindownTime /
//   ZeroOffset / Slope, requestCalibration, resetCalibrationState.
// It deliberately omits setCalibrationState: that virtual is
// ANT-only (ANTChannel drives the state machine from the
// controller side); BT40 ignores it, and the FTMS request path
// is setMode(RT_MODE_CALIBRATE), which the adapter sends in
// requestCalibration().
//
// The CALIBRATION_TYPE_* / CALIBRATION_STATE_* values come from
// CalibrationData.h (moved to core with this extension), so
// there is one source of truth for the state machine.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.

#ifndef _GC_ICalibration_h
#define _GC_ICalibration_h 1

#include "CalibrationData.h"

#include <cstdint>

class ICalibration {
public:
    virtual ~ICalibration() = default;
    // CALIBRATION_TYPE_* (CalibrationData.h).
    virtual uint8_t calibrationType() const = 0;
    // CALIBRATION_STATE_* (CalibrationData.h).
    virtual uint8_t calibrationState() const = 0;
    // kph the rider must hold during spin-down.
    virtual double calibrationTargetSpeed() const = 0;
    // Spin-down coast time, milliseconds.
    virtual uint16_t calibrationSpindownTime() const = 0;
    virtual uint16_t calibrationZeroOffset() const = 0;
    virtual uint16_t calibrationSlope() const = 0;
    // Enter calibration (FTMS: spin-down start). The device
    // drives the state machine from there; the caller polls
    // calibrationState().
    virtual void requestCalibration() = 0;
    virtual void resetCalibrationState() = 0;
};

#endif // _GC_ICalibration_h
