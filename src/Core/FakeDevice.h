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

// Phase 3: FakeDevice (core/view split, TDD section 14, Phase 3,
// and section 15.1/15.4: lag, noise, dropout, ERG convergence,
// slope response, manual ticking).
//
// A deterministic stand-in for a real device, for unit and
// integration tests. No hardware, no threads, no Qt timer, no
// global clock. The caller drives it with tick(now_ms), exactly
// as a future TrainSession will, and the device emits samples
// through its ISampleSink. TDD 15.4 is explicit: do not add a
// Qt timer to FakeDevice just to make tests pass; manual
// ticking is the feature.
//
// Configuration (FakeDeviceConfig, all fields documented here):
//   name:            device name; info().id is "fake:<name>".
//   is_trainer:      whether info() reports a controllable trainer.
//   has_power/hr/speed_cadence: capability flags in info().
//   script:          samples with monotonic_ms times, in
//                    non-decreasing order. Emitted in order.
//   lag_ms:          a script sample is emitted when
//                    script_time + lag_ms <= now_ms. This models
//                    device latency; it does not change the
//                    sample's monotonic_ms, which stays at its
//                    script time.
//   noise_watts/noise_hr: uniform noise amplitude, applied to
//                    watts and HR respectively. Zero means exact
//                    script values.
//   noise_seed:      seed for the deterministic LCG used for
//                    noise. Same seed and same tick sequence give
//                    the same output, every run. Not std::rand
//                    (which is global and not reproducible across
//                    test order).
//   dropouts:        intervals [start_ms, end_ms) in script time
//                    for script samples, and in now_ms for
//                    generated samples, during which no sample
//                    is emitted. A dropped script sample is
//                    skipped permanently (it is not emitted late);
//                    a generated sample during a dropout is
//                    simply not generated that tick.
//   erg_tau_ms:      first-order ERG time constant. Reported
//                    watts move toward the ERG target by
//                    factor 1 - exp(-dt/tau) per tick, never
//                    overshooting.
//   base_speed_kph/base_cadence/base_hr: values for generated
//                    samples (no script, or script exhausted).
//   connect_fails:   if true, connect() returns false (for
//                    testing disconnect/error paths in Phase 4).
//
// Behaviour rules, pinned by the Phase 3 unit tests:
//  - connect()/disconnect()/start()/stop() gate ticking. A
//    disconnected or stopped device emits nothing, and a
//    device with a null sink emits nowhere (it does not crash).
//    start() resets the script index and the ERG state to the
//    first script watts (or 0 with no script).
//  - Script emission is in order, one pass: once a script
//    sample is emitted or dropped, it is never emitted again,
//    even if tick() is later called with an earlier now.
//    tick() now values must be non-decreasing for generated
//    samples to be sensible; the device does not enforce this
//    beyond clamping negative dt to 0.
//  - ERG override: when an ERG target is active, script samples
//    keep their HR/cadence/speed/etc., but their watts are
//    replaced by the converged ERG watts. This is the behaviour
//    a session test needs: the workout asks for 250 W, the fake
//    trainer reports watts converging to 250 W, not the script.
//  - Generated samples: only when the script was already
//    exhausted before the tick (no script at all, or script
//    finished on an earlier tick) AND a trainer command is
//    active. While a script is pending, or on the tick where
//    its last sample is emitted, only script samples are
//    emitted. A generated sample has
//    monotonic_ms = wallclock_ms = now_ms (a fake has no real
//    wall clock; that equality is documented, not hidden),
//    trainer_connected = true, power_valid = hr_valid = true,
//    and speed from the slope model below.
//  - Slope model (simple, not physics): generated speed is
//    base_speed_kph - gradient_pct * 1.0, clamped at 0, and
//    gradient_pct is the last slope/sim gradient. Resistance
//    and sim commands are recorded and readable, but resistance
//    does not change generated watts (there is no resistance
//    physics model in Phase 3); a session test asserts the
//    command was received, not a physical response.
//  - Control methods on a disconnected device are ignored
//    (per IDevice.h): they record nothing that a later connect
//    would replay.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_FakeDevice_h
#define _GC_FakeDevice_h 1

#include "IDevice.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct FakeDeviceConfig {
    std::string name = "fake";
    bool is_trainer = true;
    bool has_power = true;
    bool has_hr = true;
    bool has_speed_cadence = true;
    std::vector<TelemetrySample> script;
    int64_t lag_ms = 0;
    double noise_watts = 0;
    double noise_hr = 0;
    uint32_t noise_seed = 1;
    std::vector<std::pair<int64_t, int64_t>> dropouts;
    double erg_tau_ms = 1000;
    double base_speed_kph = 30;
    double base_cadence = 90;
    double base_hr = 120;
    bool connect_fails = false;
};

class FakeDevice : public ITrainer {
public:
    explicit FakeDevice(const FakeDeviceConfig &config)
        : config_(config) {}

    DeviceInfo info() const override {
        DeviceInfo device_info;
        device_info.id = "fake:" + config_.name;
        device_info.name = config_.name;
        device_info.transport = "virtual";
        device_info.is_controllable_trainer = config_.is_trainer;
        device_info.has_power = config_.has_power;
        device_info.has_hr = config_.has_hr;
        device_info.has_speed_cadence = config_.has_speed_cadence;
        return device_info;
    }

    bool connect() override {
        if (config_.connect_fails) {
            connected_ = false;
            return false;
        }
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
        script_index_ = 0;
        last_tick_ms_ = -1;
        noise_state_ = config_.noise_seed;
        emitted_ = 0;
        if (!config_.script.empty()) {
            current_watts_ = config_.script[0].watts;
        } else {
            current_watts_ = 0;
        }
    }

    void stop() override { started_ = false; }

    void setSampleSink(ISampleSink *sink) override { sink_ = sink; }

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

    void setSlopeMode(double gradient_pct, double weight_kg) override {
        if (!connected_) {
            return;
        }
        slope_active_ = true;
        erg_active_ = false;
        sim_active_ = false;
        slope_gradient_ = gradient_pct;
        slope_weight_kg_ = weight_kg;
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

    void setSimMode(double gradient_pct, double speed_kph) override {
        if (!connected_) {
            return;
        }
        sim_active_ = true;
        erg_active_ = false;
        slope_active_ = false;
        sim_gradient_ = gradient_pct;
        sim_speed_kph_ = speed_kph;
    }

    // Manual tick: the only way samples are produced. now_ms
    // must be non-decreasing across calls for generated samples
    // to be sensible; script emission is index-based and safe
    // regardless.
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

        // ERG convergence first, so script samples emitted this
        // tick already carry the converged value.
        if (erg_active_) {
            double factor = 1.0 - std::exp(
                -static_cast<double>(dt_ms) / config_.erg_tau_ms);
            current_watts_ += (erg_target_ - current_watts_) * factor;
        }

        bool emitted_script = false;
        // Whether the script was already exhausted before this
        // tick: generation must not also fire on the tick where
        // the last script sample is emitted, or that tick would
        // produce both a script sample and a generated one.
        bool script_exhausted_before =
            script_index_ >= config_.script.size();
        while (script_index_ < config_.script.size()) {
            const TelemetrySample &sample = config_.script[script_index_];
            if (sample.monotonic_ms + config_.lag_ms > now_ms) {
                break;
            }
            ++script_index_;
            if (inDropout(sample.monotonic_ms)) {
                continue; // dropped permanently, not emitted late
            }
            TelemetrySample emitted = sample;
            applyNoise(emitted);
            if (erg_active_) {
                emitted.watts = current_watts_;
                emitted.power_valid = true;
            }
            // Script samples keep their script monotonic time;
            // the caller (merger, Phase 2) uses tick time as
            // received time, not sample time.
            sink_->onSample(info().id, emitted);
            ++emitted_;
            emitted_script = true;
            current_watts_ = emitted.watts;
        }

        // Generated sample only if the script was already
        // exhausted before this tick (no script at all, or
        // script finished on an earlier tick) and a trainer
        // command is active. While a script is pending, or on
        // the tick where its last sample is emitted, only script
        // samples are emitted.
        (void)emitted_script;
        if (script_exhausted_before &&
            (erg_active_ || slope_active_ || sim_active_)) {
            if (!inDropout(now_ms)) {
                TelemetrySample generated;
                generated.monotonic_ms = now_ms;
                generated.wallclock_ms = now_ms;
                generated.watts = erg_active_ ? current_watts_ : 150;
                generated.heartrate = config_.base_hr;
                generated.cadence = config_.base_cadence;
                double gradient = 0;
                double speed = config_.base_speed_kph;
                if (slope_active_) {
                    gradient = slope_gradient_;
                    speed = config_.base_speed_kph - gradient;
                } else if (sim_active_) {
                    gradient = sim_gradient_;
                    speed = sim_speed_kph_;
                }
                if (speed < 0) {
                    speed = 0;
                }
                generated.speed_kph = speed;
                generated.gradient_pct = gradient;
                generated.power_valid = true;
                generated.hr_valid = true;
                generated.trainer_connected = true;
                sink_->onSample(info().id, generated);
                ++emitted_;
            }
        }
    }

    // Test introspection (not part of IDevice/ITrainer).
    bool isStarted() const { return started_; }
    double currentWatts() const { return current_watts_; }
    size_t emittedCount() const { return emitted_; }
    bool hasErgTarget() const { return has_erg_target_; }
    double lastErgTarget() const { return erg_target_; }
    double lastSlopeGradient() const { return slope_gradient_; }
    double lastSlopeWeightKg() const { return slope_weight_kg_; }
    bool hasResistance() const { return has_resistance_; }
    int lastResistanceLevel() const { return resistance_level_; }
    double lastSimGradient() const { return sim_gradient_; }
    double lastSimSpeedKph() const { return sim_speed_kph_; }

private:
    bool inDropout(int64_t time_ms) const {
        for (const auto &interval : config_.dropouts) {
            if (time_ms >= interval.first && time_ms < interval.second) {
                return true;
            }
        }
        return false;
    }

    // Uniform noise in [-amplitude, +amplitude], from a small
    // LCG. Deterministic for a given seed and call sequence.
    double nextNoise(double amplitude) {
        if (amplitude == 0) {
            return 0;
        }
        noise_state_ = noise_state_ * 1664525u + 1013904223u;
        double uniform = static_cast<double>(noise_state_ >> 8) /
                         static_cast<double>(1u << 24);
        return (uniform * 2.0 - 1.0) * amplitude;
    }

    void applyNoise(TelemetrySample &sample) {
        if (config_.noise_watts != 0) {
            sample.watts += nextNoise(config_.noise_watts);
            if (sample.watts < 0) {
                sample.watts = 0;
            }
        }
        if (config_.noise_hr != 0) {
            sample.heartrate += nextNoise(config_.noise_hr);
            if (sample.heartrate < 0) {
                sample.heartrate = 0;
            }
        }
    }

    FakeDeviceConfig config_;
    ISampleSink *sink_ = nullptr;
    bool connected_ = false;
    bool started_ = false;
    size_t script_index_ = 0;
    int64_t last_tick_ms_ = -1;
    uint32_t noise_state_ = 1;
    size_t emitted_ = 0;
    double current_watts_ = 0;
    bool erg_active_ = false;
    bool has_erg_target_ = false;
    double erg_target_ = 0;
    bool slope_active_ = false;
    double slope_gradient_ = 0;
    double slope_weight_kg_ = 0;
    bool has_resistance_ = false;
    int resistance_level_ = 0;
    bool sim_active_ = false;
    double sim_gradient_ = 0;
    double sim_speed_kph_ = 0;
};

#endif // _GC_FakeDevice_h
