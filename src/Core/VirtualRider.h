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

// Zwift-like game support, step 3: VirtualRider (core/view split).
//
// A trainer with no hardware, for playing the game without one.
// The app drives the rider's watts into it with setVirtualWatts
// (keyboard/buttons are the game's "legs" until a real trainer
// exists), and it reports speed from the surfaced physics model
// (computeInstantSpeed, step 2), accumulating distance and
// altitude from its own speed each tick — the way a real trainer
// reports cumulative distance, so the session merges it, the
// recorder gets it, and the app reads it.
//
// Configuration (VirtualRiderConfig, all fields documented here):
//   name:            device name; info().id is "virtual:<name>".
//   rider_weight_kg / bike_weight_kg: physics mass. The session
//                    passes the rider weight to setSlopeMode, but
//                    this device uses its configured weights for
//                    physics in all modes (the C ABI builds the
//                    device and the session profile from the same
//                    AthleteProfile, so they cannot drift).
//   erg_tau_ms:      first-order ERG time constant, as in
//                    FakeDevice: reported watts move toward the
//                    ERG target by factor 1 - exp(-dt/tau) per
//                    tick, never overshooting.
//   base_cadence/base_hr: values for generated samples. Fixed
//                    defaults for now (a real rider's cadence and
//                    HR are inputs of their own; synthesising them
//                    from watts is later work).
//   crr / Cd / frontal_area_m2 / temp_kelvin: physics parameters
//                    forwarded to computeInstantSpeed; defaults
//                    are the function's defaults, which are also
//                    the defaults the GUI train view uses.
//
// Behaviour rules:
//  - connect()/disconnect()/start()/stop() gate ticking, as in
//    FakeDevice. start() resets distance and altitude to 0 (a
//    new ride), the ERG state to the current virtual watts, and
//    the tick clock.
//  - Watts: in ERG mode the reported watts converge to the ERG
//    target; otherwise they are the last virtual watts the app
//    set. In ERG mode the speed still comes from the physics
//    model (converged watts, current gradient), so an ERG ride
//    on a gradient still climbs at the ERG watts.
//  - Speed: computeInstantSpeed(rider + bike weight, gradient,
//    altitude, watts) each tick. Gradient is the last slope or
//    sim gradient, whichever mode is active. The model is
//    instantaneous — speed follows watts and gradient at once,
//    with no momentum. That is the documented limitation of the
//    instant function; the BicycleSim momentum model is later
//    work, not this device.
//  - Distance and altitude accumulate from speed: distance +=
//    speed * dt; altitude += speed * dt * sin(atan(gradient)).
//  - Samples are emitted only while a trainer command is active
//    (ERG, slope, or sim), as in FakeDevice. With no command
//    the device is silent, so the session's stale detection
//    still works. The game always has a command active: the app
//    sets the course gradient as the sim gradient every frame.
//  - Control methods on a disconnected device are ignored (per
//    IDevice.h): they record nothing that a later connect would
//    replay.
//  - setSlopeMode's weight_kg and setSimMode's speed_kph are
//    ignored: physics uses the configured weights, and speed is
//    computed, not given. Resistance is recorded and readable,
//    but does not change generated watts (there is no
//    resistance physics model here, as in FakeDevice).
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_VirtualRider_h
#define _GC_VirtualRider_h 1

#include "IDevice.h"
#include "PhysicsUtility.h"

#include <cmath>
#include <cstdint>
#include <string>

struct VirtualRiderConfig {
    std::string name = "rider";
    double rider_weight_kg = 75;
    double bike_weight_kg = 8;
    double erg_tau_ms = 1000;
    double base_cadence = 90;
    double base_hr = 120;
    double crr = 0.004;
    double Cd = 1.0;
    double frontal_area_m2 = 0.5;
    double temp_kelvin = 293.15;
};

class VirtualRider : public ITrainer {
public:
    explicit VirtualRider(const VirtualRiderConfig &config)
        : config_(config) {}

    DeviceInfo info() const override {
        DeviceInfo device_info;
        device_info.id = "virtual:" + config_.name;
        device_info.name = config_.name;
        device_info.transport = "virtual";
        device_info.is_controllable_trainer = true;
        device_info.has_power = true;
        device_info.has_hr = true;
        device_info.has_speed_cadence = true;
        return device_info;
    }

    bool connect() override {
        connected_ = true;
        return true;
    }

    void disconnect() override {
        connected_ = false;
        started_ = false;
    }

    bool isConnected() const override { return connected_; }

    void start() override {
        if (!connected_) {
            return;
        }
        started_ = true;
        last_tick_ms_ = -1;
        emitted_ = 0;
        distance_m_ = 0;
        altitude_m_ = 0;
        speed_kph_ = 0;
        current_watts_ = virtual_watts_;
    }

    void stop() override { started_ = false; }

    void setSampleSink(ISampleSink *sink) override { sink_ = sink; }

    // The app's watts input: the rider's legs, until a real
    // trainer exists. Takes effect on the next tick.
    void setVirtualWatts(double watts) {
        if (watts < 0) {
            watts = 0;
        }
        virtual_watts_ = watts;
    }

    void setErgMode(double target_watts) override {
        if (!connected_) {
            return;
        }
        erg_active_ = true;
        slope_active_ = false;
        sim_active_ = false;
        erg_target_ = target_watts;
        has_erg_target_ = true;
    }

    void setSlopeMode(double gradient_pct, double /*weight_kg*/) override {
        if (!connected_) {
            return;
        }
        slope_active_ = true;
        erg_active_ = false;
        sim_active_ = false;
        slope_gradient_ = gradient_pct;
    }

    void setResistanceMode(int level_0_100) override {
        if (!connected_) {
            return;
        }
        if (level_0_100 < 0) {
            level_0_100 = 0;
        }
        if (level_0_100 > 100) {
            level_0_100 = 100;
        }
        resistance_level_ = level_0_100;
        has_resistance_ = true;
    }

    void setSimMode(double gradient_pct, double /*speed_kph*/) override {
        if (!connected_) {
            return;
        }
        sim_active_ = true;
        erg_active_ = false;
        slope_active_ = false;
        sim_gradient_ = gradient_pct;
    }

    // Manual tick: the only way samples are produced. now_ms
    // must be non-decreasing across calls for the accumulated
    // distance and altitude to be sensible; negative dt is
    // clamped to 0, as in FakeDevice.
    void tick(int64_t now_ms) {
        if (!connected_ || !started_ || sink_ == nullptr) {
            return;
        }
        int64_t dt_ms = 0;
        if (last_tick_ms_ >= 0) {
            dt_ms = now_ms - last_tick_ms_;
            if (dt_ms < 0) {
                dt_ms = 0;
            }
        }
        last_tick_ms_ = now_ms;

        if (!erg_active_ && !slope_active_ && !sim_active_) {
            return; // no command: silent, as in FakeDevice
        }

        // ERG convergence first, so this tick's sample already
        // carries the converged value.
        if (erg_active_) {
            double factor = 1.0 - std::exp(
                -static_cast<double>(dt_ms) / config_.erg_tau_ms);
            current_watts_ += (erg_target_ - current_watts_) * factor;
        } else {
            current_watts_ = virtual_watts_;
        }

        double gradient = 0;
        if (slope_active_) {
            gradient = slope_gradient_;
        } else if (sim_active_) {
            gradient = sim_gradient_;
        }

        double total_weight_kg =
            config_.rider_weight_kg + config_.bike_weight_kg;
        speed_kph_ = computeInstantSpeed(
            total_weight_kg, gradient, altitude_m_, current_watts_,
            config_.crr, 1.0, config_.Cd, config_.frontal_area_m2,
            config_.temp_kelvin);
        if (speed_kph_ < 0) {
            speed_kph_ = 0;
        }

        // Accumulate distance and altitude from speed. Altitude
        // gain is speed * dt * sin(atan(gradient)), with the
        // gradient as a fraction (8% = 0.08).
        double dt_s = static_cast<double>(dt_ms) / 1000.0;
        double speed_ms = speed_kph_ / 3.6;
        double gradient_fraction = gradient / 100.0;
        double sin_slope = gradient_fraction /
            std::sqrt(1.0 + gradient_fraction * gradient_fraction);
        distance_m_ += speed_ms * dt_s;
        altitude_m_ += speed_ms * dt_s * sin_slope;

        TelemetrySample sample;
        sample.monotonic_ms = now_ms;
        sample.wallclock_ms = now_ms;
        sample.watts = current_watts_;
        sample.heartrate = config_.base_hr;
        sample.cadence = config_.base_cadence;
        sample.speed_kph = speed_kph_;
        sample.distance_m = distance_m_;
        sample.altitude_m = altitude_m_;
        sample.gradient_pct = gradient;
        sample.power_valid = true;
        sample.hr_valid = true;
        sample.trainer_connected = true;
        sink_->onSample(info().id, sample);
        ++emitted_;
    }

    // Test introspection (not part of IDevice/ITrainer).
    bool isStarted() const { return started_; }
    double currentWatts() const { return current_watts_; }
    double virtualWatts() const { return virtual_watts_; }
    double speedKph() const { return speed_kph_; }
    double distanceM() const { return distance_m_; }
    double altitudeM() const { return altitude_m_; }
    size_t emittedCount() const { return emitted_; }
    bool hasErgTarget() const { return has_erg_target_; }
    double lastErgTarget() const { return erg_target_; }
    double lastSlopeGradient() const { return slope_gradient_; }
    double lastSimGradient() const { return sim_gradient_; }
    bool hasResistance() const { return has_resistance_; }
    int lastResistanceLevel() const { return resistance_level_; }

private:
    VirtualRiderConfig config_;
    ISampleSink *sink_ = nullptr;
    bool connected_ = false;
    bool started_ = false;
    int64_t last_tick_ms_ = -1;
    size_t emitted_ = 0;
    double virtual_watts_ = 0;
    double current_watts_ = 0;
    double speed_kph_ = 0;
    double distance_m_ = 0;
    double altitude_m_ = 0;
    bool erg_active_ = false;
    bool has_erg_target_ = false;
    double erg_target_ = 0;
    bool slope_active_ = false;
    double slope_gradient_ = 0;
    bool sim_active_ = false;
    double sim_gradient_ = 0;
    bool has_resistance_ = false;
    int resistance_level_ = 0;
};

#endif // _GC_VirtualRider_h
