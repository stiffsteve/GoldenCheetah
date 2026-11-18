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

#include "Bt40Backend.h"

#include "BT40Controller.h"
#include "BT40Device.h"
#include "Bt40Scanner.h"
#include "TrainSidebar.h" // RT_MODE_* defines

#include <memory>

namespace {

class Bt40ControllerBackend : public IBt40Backend {
public:
    explicit Bt40ControllerBackend(QBluetoothDeviceInfo target)
        : target_(std::move(target))
    {
        // The controller needs a DeviceConfiguration pointer; a
        // default one is enough — discovery is never started on
        // it (see the header comment), so its profile is unused.
        // It must outlive the controller, hence a member.
        controller_ = std::make_unique<BT40Controller>(nullptr, &dc_);
        // Parented to the controller: the device's updateValue and
        // deviceDisconnected call back into it via dynamic_cast,
        // and QObject parentage deletes the device with it.
        device_ = new BT40Device(controller_.get(), target_);
    }

    ~Bt40ControllerBackend() override
    {
        disconnect();
    }

    bool connect() override
    {
        if (device_ != nullptr && device_->isConnected()) {
            return true; // idempotent
        }
        if (device_ == nullptr) {
            return false;
        }
        device_->connectDevice();
        // The honest synchronous answer: is there a local BT
        // adapter at all? The link itself comes up async.
        return controller_->find();
    }

    void disconnect() override
    {
        if (device_ != nullptr) {
            device_->disconnectDevice();
        }
    }

    bool isConnected() const override
    {
        return device_ != nullptr && device_->isConnected();
    }

    Bt40Telemetry telemetry() override
    {
        RealtimeData rt;
        controller_->getRealtimeData(rt);
        Bt40Telemetry t;
        t.watts = rt.getWatts();
        t.speed_kph = rt.getSpeed();
        t.cadence = rt.getCadence();
        t.heartrate = rt.getHr();
        return t;
    }

    void setErg(double watts) override
    {
        device_->setMode(RT_MODE_ERGO);
        device_->setLoad(watts); // dispatches to setLoadErg
    }

    void setSlope(double gradient_pct, double weight_kg) override
    {
        device_->setMode(RT_MODE_SLOPE);
        device_->setWeight(weight_kg);
        device_->setGradient(gradient_pct);
    }

    void setSim(double gradient_pct) override
    {
        // Weight stays cached on the device from the last
        // setSlope/setWeight (default 80 kg); the session's Sim
        // command carries no weight.
        device_->setMode(RT_MODE_SLOPE);
        device_->setGradient(gradient_pct);
    }

    void setResistance(double level_0_1) override
    {
        device_->setLoadIntensity(level_0_1);
    }

    void requestCalibration() override
    {
        // FTMS: sends spin-down start when supported.
        device_->setMode(RT_MODE_CALIBRATE);
    }

    void resetCalibration() override
    {
        device_->resetCalibrationState();
    }

    uint8_t calibrationType() override
    {
        return device_->getCalibrationType();
    }

    uint8_t calibrationState() override
    {
        return device_->getCalibrationState();
    }

    double calibrationTargetSpeed() override
    {
        return device_->getCalibrationTargetSpeed();
    }

    uint16_t calibrationSpindownTime() override
    {
        return device_->getCalibrationSpindownTime();
    }

    uint16_t calibrationZeroOffset() override
    {
        return device_->getCalibrationZeroOffset();
    }

    uint16_t calibrationSlope() override
    {
        return device_->getCalibrationSlope();
    }

private:
    QBluetoothDeviceInfo target_;
    DeviceConfiguration dc_;
    std::unique_ptr<BT40Controller> controller_;
    BT40Device *device_ = nullptr; // owned via QObject parentage
};

} // namespace

std::unique_ptr<IBt40Backend> makeBt40Backend(
    const QBluetoothDeviceInfo &target)
{
    return std::make_unique<Bt40ControllerBackend>(target);
}

int discoverAndRegisterBt40Devices(DeviceManager &manager,
                                   int timeout_ms)
{
    int registered = 0;
    for (const QBluetoothDeviceInfo &target :
         Bt40Scanner::discover(timeout_ms)) {
        DeviceInfo info = Bt40Scanner::deviceInfoFor(target);
        manager.registerDevice(
            info, [info, target](const DeviceInfo &) {
                return std::make_unique<Bt40DeviceAdapter>(
                    info, makeBt40Backend(target));
            });
        ++registered;
    }
    return registered;
}
