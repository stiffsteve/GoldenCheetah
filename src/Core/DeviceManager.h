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

// Phase 3: DeviceManager (core/view split, TDD section 14, Phase 3,
// and section 7.5: "Discovery is separate from connection.
// DeviceManager::scan() returns DeviceInfo list.
// DeviceManager::open(id) returns an IDevice*").
//
// The manager owns discovery, not devices (TDD Q6: the session
// owns devices passed to it, the manager owns discovery). That
// ownership split is implemented literally here:
//   - scan() returns DeviceInfo values, never devices. Calling
//     scan() does not connect, does not create a device, and
//     does not change any device state.
//   - open(id) creates a fresh device via the registered factory
//     and returns it as a unique_ptr. The caller owns it from
//     that moment; the manager keeps no pointer to it, and
//     opening the same id twice gives two independent devices
//     with no shared state.
//   - The manager keeps its registry (DeviceInfo plus factory)
//     for the life of the manager. A device created by open()
//     must not outlive the factory's captured state; factories
//     in tests capture by value (a config), and that rule is
//     documented here rather than enforced by the type system.
//
// Registration, not hardware discovery, in Phase 3:
//   - registerDevice(info, factory) adds a known device.
//     Registering the same id twice replaces the previous
//     registration (last registration wins), so a test can
//     override a fake's script without creating a second manager.
//   - registerFake(config) is a convenience that registers a
//     FakeDevice with id "fake:<name>" and a factory that
//     creates a fresh FakeDevice from the given config on each
//     open(). The config is copied into the factory, so the
//     caller can destroy its original config after registering.
//   - No real hardware discovery in Phase 3: scan() returns
//     only registered devices. A future ANT/BLE adapter will
//     register real devices as they are discovered, but that
//     adapter is not here, and scan() must not be mistaken for
//     a hardware scan.
//   - open() of an unknown id returns nullptr, not a crash and
//     not a default device. Callers must check for nullptr;
//     a session that cannot open its configured trainer must
//     report that, not proceed with a silent substitute.
//
// No threads, no Qt, no hardware access in this header. The
// manager is a registry; discovery of real hardware, when it
// arrives, will be a separate adapter that calls registerDevice().
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_DeviceManager_h
#define _GC_DeviceManager_h 1

#include "FakeDevice.h"
#include "IDevice.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class DeviceManager {
public:
    using Factory =
        std::function<std::unique_ptr<IDevice>(const DeviceInfo &info)>;

    void registerDevice(const DeviceInfo &info, Factory factory) {
        for (auto &entry : entries_) {
            if (entry.info.id == info.id) {
                entry.info = info;
                entry.factory = std::move(factory);
                return;
            }
        }
        entries_.push_back(Entry{info, std::move(factory)});
    }

    // Register a fake device from its config. The config is
    // copied into the factory, so each open() creates a fresh
    // FakeDevice with the same script, lag, noise, and dropout
    // settings, but independent runtime state.
    void registerFake(const FakeDeviceConfig &config) {
        DeviceInfo info;
        info.id = "fake:" + config.name;
        info.name = config.name;
        info.transport = "virtual";
        info.is_controllable_trainer = config.is_trainer;
        info.has_power = config.has_power;
        info.has_hr = config.has_hr;
        info.has_speed_cadence = config.has_speed_cadence;
        registerDevice(info, [config](const DeviceInfo &) {
            return std::make_unique<FakeDevice>(config);
        });
    }

    // Discovery: returns registered devices, in registration
    // order. Does not connect, does not create devices.
    std::vector<DeviceInfo> scan() const {
        std::vector<DeviceInfo> result;
        for (const auto &entry : entries_) {
            result.push_back(entry.info);
        }
        return result;
    }

    // Connection: creates a fresh device, caller-owned.
    // Unknown id returns nullptr.
    std::unique_ptr<IDevice> open(const std::string &id) const {
        for (const auto &entry : entries_) {
            if (entry.info.id == id) {
                return entry.factory(entry.info);
            }
        }
        return nullptr;
    }

    bool hasDevice(const std::string &id) const {
        for (const auto &entry : entries_) {
            if (entry.info.id == id) {
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        DeviceInfo info;
        Factory factory;
    };
    std::vector<Entry> entries_;
};

#endif // _GC_DeviceManager_h
