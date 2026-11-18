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

// Production backend for the FTMS adapter (Bt40DeviceAdapter.h).
// Wraps a BT40Controller (telemetry hub + parent) and one
// BT40Device created directly for the scanned target. This file
// is Qt and is compiled into the main GUI build only — it is
// deliberately NOT linked into the adapter unit tests (see the
// adapter header), so the tests stay free of the BT40 link
// closure.
//
// Design (see the scope doc): the controller's discovery path
// is not used. Its allow-list needs a deviceProfile
// ("name;address;uuid") whose uuid semantics on Linux are
// uncertain, and an empty allow-list connects every discovered
// device (the controller then aggregates all of them into one
// RealtimeData). Creating one BT40Device for the scanned target
// gives exact single-target control. The controller's `devices`
// list stays empty; the backend calls the device methods
// directly and manages the device lifetime (disconnect on
// teardown; QObject parentage deletes it with the controller).
// The controller's setNotification signal is never connected,
// so no toast leaks to the GUI.

#ifndef _GC_Bt40Backend_h
#define _GC_Bt40Backend_h 1

#include "Bt40DeviceAdapter.h"
#include "Core/DeviceManager.h"

#include <QBluetoothDeviceInfo>

#include <memory>

// Build a production backend bound to a discovered BT device
// (its QBluetoothDeviceInfo, captured at scan time).
std::unique_ptr<IBt40Backend> makeBt40Backend(
    const QBluetoothDeviceInfo &target);

// Blocking scan + registration: runs the scanner, then registers
// each discovered FTMS trainer in the manager (id
// "ble:<address>") with a factory that builds a Bt40DeviceAdapter
// bound to it. Returns the number of trainers registered.
// Requires a QCoreApplication event loop (the caller provides
// it). This is the integration point for the future GUI switch;
// nothing calls it yet.
int discoverAndRegisterBt40Devices(DeviceManager &manager,
                                   int timeout_ms);

#endif // _GC_Bt40Backend_h
