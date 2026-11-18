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

// Real trainer, FTMS adapter (core/view split).
// Thin IDevice adapter over the legacy BT40 stack: it translates
// the core session's commands into the legacy controller/device
// calls and the legacy telemetry snapshot into TelemetrySamples.
// It does NOT re-implement any protocol, GATT handling, or
// proprietary load type — those stay in BT40Controller/BT40Device
// (strangler pattern; the legacy GUI keeps working).
//
// Threading — queue-and-drain, no core change, no mutex:
// the adapter exposes tick(), called by the session owner on the
// session thread before the session tick (the same shape as
// gc_session_tick ticking device then session). Each tick polls
// the backend's latest snapshot and emits one sample. The BT
// thread only mutates the controller's RealtimeData, as it does
// in legacy; all sample emission happens on the session thread.
//
// This header is intentionally Qt-free (core + std only) so the
// adapter unit tests link it without dragging in the BT40 stack.
// The production backend (Bt40Backend.h, Qt) is a separate file.
//
// Sample mapping (see the scope doc for the full table):
//  - watts/cadence/speed/hr from the snapshot; power_valid while
//    connected and started; hr_valid when heartrate > 0.
//  - distance_m accumulated by the adapter from speed x dt
//    (FTMS indoor-bike notifications do not carry distance —
//    ftms_parse_indoor_bike_data discards the 24-bit field).
//  - altitude_m is 0 (FTMS does not report it).
//  - gradient_pct is the last commanded gradient; target_watts
//    the last ERG target.
// Commands forward on every session tick with no dedupe: the
// session re-sending every tick is the resend mechanism against
// a lost BLE write.
// Disconnect: isConnected() == false, and tick() emits nothing
// while disconnected, so the session's 3 s stale timeout and its
// PausedWithError detection (trainer was connected, now is not)
// both work. The adapter never connects the controller's
// setNotification signal (no toasts) and does not surface the
// device's auto-reconnect timer.

#ifndef _GC_Bt40DeviceAdapter_h
#define _GC_Bt40DeviceAdapter_h 1

#include "Core/IDevice.h"

#include <cstdint>
#include <memory>

// Latest telemetry snapshot from the BT40 stack, in plain types.
// The production backend converts RealtimeData into this; the
// stub backend in tests fills it directly.
struct Bt40Telemetry {
    double watts = 0;
    double speed_kph = 0;
    double cadence = 0;
    double heartrate = 0;
};

// Seam between the adapter and the BT40 stack. The production
// implementation (Bt40Backend.cpp) wraps BT40Controller + one
// BT40Device; tests stub this interface — deterministic, no
// radio, no Qt event loop.
class IBt40Backend {
public:
    virtual ~IBt40Backend() = default;
    // Initiate the GATT connection to the target. Returns false
    // if the transport cannot start (no local BT adapter).
    // Idempotent: reconnecting an already-connected device
    // returns true.
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    // GATT link actually up (not the legacy "intent" flag).
    virtual bool isConnected() const = 0;
    virtual Bt40Telemetry telemetry() = 0;
    virtual void setErg(double watts) = 0;
    virtual void setSlope(double gradient_pct, double weight_kg) = 0;
    virtual void setSim(double gradient_pct) = 0;
    // Resistance as a 0..1 level (linear across the device's
    // reported range, capped by ftms_resistance_cap).
    virtual void setResistance(double level_0_1) = 0;
    // Calibration: request = enter calibration mode (FTMS:
    // spin-down start); reset clears the device's state.
    virtual void requestCalibration() = 0;
    virtual void resetCalibration() = 0;
    virtual uint8_t calibrationType() = 0;
    virtual uint8_t calibrationState() = 0;
    virtual double calibrationTargetSpeed() = 0;
    virtual uint16_t calibrationSpindownTime() = 0;
    virtual uint16_t calibrationZeroOffset() = 0;
    virtual uint16_t calibrationSlope() = 0;
};

class Bt40DeviceAdapter : public ITrainer {
public:
    // info: the core DeviceInfo (id "ble:<address>"). backend:
    // the BT40 backend, owned by the adapter (must be non-null).
    Bt40DeviceAdapter(DeviceInfo info,
                      std::unique_ptr<IBt40Backend> backend);

    DeviceInfo info() const override { return info_; }
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    void start() override;
    void stop() override;
    void setSampleSink(ISampleSink *sink) override;

    void setErgMode(double target_watts) override;
    void setSlopeMode(double gradient_pct, double weight_kg) override;
    void setResistanceMode(int level_0_100) override;
    void setSimMode(double gradient_pct, double speed_kph) override;

    // Queue-and-drain pump (see the header comment). Emits one
    // sample per call while started and connected; emits nothing
    // otherwise. now_monotonic_ms is the session clock, explicit
    // like VirtualRider::tick (TDD 15.4 — manual ticking is the
    // feature). Distance accumulates from speed x dt since the
    // last emitted sample.
    void tick(int64_t now_monotonic_ms);

    // Calibration, forwarded to the backend. These are wired to
    // the core ICalibration interface in the next step; they
    // exist here already so the backend contract is complete.
    uint8_t calibrationType();
    uint8_t calibrationState();
    double calibrationTargetSpeed();
    uint16_t calibrationSpindownTime();
    uint16_t calibrationZeroOffset();
    uint16_t calibrationSlope();
    void requestCalibration();
    void resetCalibrationState();

private:
    DeviceInfo info_;
    std::unique_ptr<IBt40Backend> backend_;
    ISampleSink *sink_ = nullptr;
    bool started_ = false;
    double last_gradient_pct_ = 0;
    double last_target_watts_ = 0;
    double distance_m_ = 0;
    // -1: no sample emitted yet (no distance accumulation yet).
    int64_t last_emit_ms_ = -1;
};

#endif // _GC_Bt40DeviceAdapter_h
