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

#include "Bt40DeviceAdapter.h"

#include <algorithm>
#include <chrono>

namespace {

int64_t wallclockNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

Bt40DeviceAdapter::Bt40DeviceAdapter(
    DeviceInfo info, std::unique_ptr<IBt40Backend> backend)
    : info_(std::move(info)), backend_(std::move(backend))
{
}

bool Bt40DeviceAdapter::connect()
{
    return backend_->connect();
}

void Bt40DeviceAdapter::disconnect()
{
    started_ = false;
    last_emit_ms_ = -1; // do not accumulate distance across the gap
    backend_->disconnect();
}

bool Bt40DeviceAdapter::isConnected() const
{
    return backend_->isConnected();
}

void Bt40DeviceAdapter::start()
{
    started_ = true;
}

void Bt40DeviceAdapter::stop()
{
    started_ = false;
    last_emit_ms_ = -1; // do not accumulate distance across the gap
}

void Bt40DeviceAdapter::setSampleSink(ISampleSink *sink)
{
    sink_ = sink;
}

void Bt40DeviceAdapter::setErgMode(double target_watts)
{
    // Per IDevice: a command on a disconnected trainer records
    // nothing a later connect would replay.
    if (!backend_->isConnected()) {
        return;
    }
    last_target_watts_ = target_watts;
    last_gradient_pct_ = 0; // ERG mode simulates no gradient
    backend_->setErg(target_watts);
}

void Bt40DeviceAdapter::setSlopeMode(double gradient_pct,
                                     double weight_kg)
{
    if (!backend_->isConnected()) {
        return;
    }
    last_gradient_pct_ = gradient_pct;
    last_target_watts_ = 0;
    backend_->setSlope(gradient_pct, weight_kg);
}

void Bt40DeviceAdapter::setResistanceMode(int level_0_100)
{
    if (!backend_->isConnected()) {
        return;
    }
    last_target_watts_ = 0;
    double level = std::max(0, std::min(100, level_0_100)) / 100.0;
    backend_->setResistance(level);
}

void Bt40DeviceAdapter::setSimMode(double gradient_pct,
                                   double speed_kph)
{
    // The BT40 stack has no sim-speed setpoint: speed is reported
    // by the trainer, not commanded. The session's Sim speed is
    // always 0 anyway (TrainSession::sendToTrainer).
    (void)speed_kph;
    if (!backend_->isConnected()) {
        return;
    }
    last_gradient_pct_ = gradient_pct;
    last_target_watts_ = 0;
    backend_->setSim(gradient_pct);
}

void Bt40DeviceAdapter::tick(int64_t now_monotonic_ms)
{
    if (!started_ || sink_ == nullptr) {
        return;
    }
    if (!backend_->isConnected()) {
        return;
    }
    Bt40Telemetry t = backend_->telemetry();

    // Distance accumulates from speed since the last emitted
    // sample (FTMS notifications carry no distance).
    if (last_emit_ms_ >= 0) {
        double dt_s = (now_monotonic_ms - last_emit_ms_) / 1000.0;
        if (dt_s > 0) {
            distance_m_ += t.speed_kph / 3.6 * dt_s;
        }
    }
    last_emit_ms_ = now_monotonic_ms;

    TelemetrySample sample;
    sample.monotonic_ms = now_monotonic_ms;
    sample.wallclock_ms = wallclockNowMs();
    sample.watts = t.watts;
    sample.heartrate = t.heartrate;
    sample.cadence = t.cadence;
    sample.speed_kph = t.speed_kph;
    sample.distance_m = distance_m_;
    sample.altitude_m = 0; // FTMS does not report altitude
    sample.gradient_pct = last_gradient_pct_;
    sample.target_watts = last_target_watts_;
    sample.power_valid = true;
    sample.hr_valid = t.heartrate > 0; // 0 means no HR data
    sample.trainer_connected = true;
    sink_->onSample(info_.id, sample);
}

uint8_t Bt40DeviceAdapter::calibrationType() const
{
    return backend_->calibrationType();
}

uint8_t Bt40DeviceAdapter::calibrationState() const
{
    return backend_->calibrationState();
}

double Bt40DeviceAdapter::calibrationTargetSpeed() const
{
    return backend_->calibrationTargetSpeed();
}

uint16_t Bt40DeviceAdapter::calibrationSpindownTime() const
{
    return backend_->calibrationSpindownTime();
}

uint16_t Bt40DeviceAdapter::calibrationZeroOffset() const
{
    return backend_->calibrationZeroOffset();
}

uint16_t Bt40DeviceAdapter::calibrationSlope() const
{
    return backend_->calibrationSlope();
}

void Bt40DeviceAdapter::requestCalibration()
{
    backend_->requestCalibration();
}

void Bt40DeviceAdapter::resetCalibrationState()
{
    backend_->resetCalibration();
}
