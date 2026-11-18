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

// Blocking BLE discovery for the FTMS adapter.
//
// discover() runs a QBluetoothDeviceDiscoveryAgent to completion
// (or a timeout) on a local event loop and returns the FTMS
// trainers found. It does not connect to anything and does not
// touch the DeviceManager — registration lives in Bt40Backend.cpp
// (discoverAndRegisterBt40Devices), so this file stays free of
// the backend and its link closure, and its pure helpers are
// unit-testable headlessly.
//
// Requires a QCoreApplication event loop (the caller provides
// it). No radio work happens outside discover().

#ifndef _GC_Bt40Scanner_h
#define _GC_Bt40Scanner_h 1

#include "Core/DeviceInfo.h"

#include <QBluetoothDeviceInfo>

#include <QList>

class Bt40Scanner {
public:
    // Blocking scan. Returns the discovered FTMS trainers (their
    // full QBluetoothDeviceInfo, captured for the adapter). An
    // empty list means none found (or no BT adapter, or the
    // timeout fired first).
    static QList<QBluetoothDeviceInfo> discover(int timeout_ms);

    // Pure helpers, unit-tested headlessly:
    // A trainer is a Low Energy device that advertises the FTMS
    // service (0x1826) and has a Bluetooth address. The address
    // check is the Linux stable-id requirement: on macOS there is
    // no address (the legacy code falls back to the device UUID),
    // and that fallback is follow-on work, not this item.
    static bool isFtmsTrainer(const QBluetoothDeviceInfo &info);
    // Core DeviceInfo for a discovered trainer: stable id
    // "ble:<address>", transport "ble", trainer + power +
    // speed/cadence. has_hr is false: the Zwift Hub reports no
    // HR, and HR arrives (if at all) on the indoor-bike data.
    static DeviceInfo deviceInfoFor(const QBluetoothDeviceInfo &info);
};

#endif // _GC_Bt40Scanner_h
