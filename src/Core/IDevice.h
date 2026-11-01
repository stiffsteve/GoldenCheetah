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

// Phase 3: IDevice and ITrainer (core/view split, TDD section 7.5).
// One interface for all transports. Sensors and trainers are
// distinct: a power meter is not a trainer, and only trainers
// get the control methods.
//
// Design decisions, from the TDD, pinned here:
//  - No Qt types in any signature. Use std::string, double,
//    bool. A Qt adapter translates later, if one is ever needed.
//  - No widget callbacks. Devices emit samples through a sink
//    (ISampleSink::onSample), and the engine owns fan-out
//    (TelemetryBus, Phase 2, is the fan-out for merged samples;
//    a device sink is the per-device entry point, not the bus).
//  - Discovery is separate from connection. DeviceManager::scan()
//    returns DeviceInfo values; DeviceManager::open(id) returns
//    a device. This header does not do discovery itself.
//  - Pairing configuration is a list of device ids plus roles
//    (power source, HR source, controllable trainer), serialisable
//    without device code, because DeviceInfo::id is a stable
//    string, never a pointer (Phase 0).
//  - A smart trainer is both a sensor and a controllable device.
//    It exposes both roles in one object (it implements ITrainer,
//    which extends IDevice), rather than conflating them into
//    two objects that must be kept in sync.
//
// Ownership and lifecycle, pinned because the interfaces do
// not express them:
//  - The device does not own its sink. The caller sets a sink
//    with setSampleSink() and must clear it (set to nullptr)
//    before the sink is destroyed, and before the device is
//    destroyed if the sink outlives it. A device must tolerate
//    a null sink (it simply emits nowhere).
//  - connect() establishes the transport (BLE pairing, ANT
//    channel, serial open, or, for a fake, nothing). start()
//    begins data production; stop() ends it. A device that is
//    not connected, or not started, must not emit samples.
//    connect() returning false means the transport failed; the
//    device stays disconnected and start() must not emit.
//  - start()/stop() are idempotent in spirit: calling start()
//    on an already-started device, or stop() on a stopped one,
//    must not crash and must not reset state unexpectedly.
//    Implementations should treat them as state transitions,
//    not as one-shot actions.
//  - Control methods (setErgMode, etc.) are only meaningful on
//    a connected trainer. Calling them on a disconnected trainer
//    must record nothing that a later connect would replay
//    unexpectedly; FakeDevice records the last command only
//    while connected, and real adapters should do the same.
//  - These interfaces are not the legacy RealtimeController.
//    That class is a QObject with push/pull telemetry into a
//    mutable RealtimeData, calibration, and GUI ties. Converting
//    it is a separate, hardware-gated project (see the Phase 3
//    plan); this header is the target interface, not a wrapper
//    around the legacy class.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other Phase 0/1/2/3 core types.

#ifndef _GC_IDevice_h
#define _GC_IDevice_h 1

#include "DeviceInfo.h"
#include "TelemetrySample.h"

#include <string>

class ISampleSink {
public:
    virtual ~ISampleSink() = default;
    virtual void onSample(const std::string &device_id,
                          const TelemetrySample &sample) = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual DeviceInfo info() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setSampleSink(ISampleSink *sink) = 0;
};

// Only trainers get this. A power meter is not a trainer.
class ITrainer : public IDevice {
public:
    virtual void setErgMode(double target_watts) = 0;
    virtual void setSlopeMode(double gradient_pct, double weight_kg) = 0;
    virtual void setResistanceMode(int level_0_100) = 0;
    virtual void setSimMode(double gradient_pct, double speed_kph) = 0;
};

#endif // _GC_IDevice_h
