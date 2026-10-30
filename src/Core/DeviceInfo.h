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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

// Phase 0 core type (core/view split, TDD section 7.1).
// Describes a device without holding a pointer to it.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// The id is a stable string for configuration, never a pointer, so
// pairing configuration can be serialised (list of device ids plus
// roles) without depending on device or GUI code.

#ifndef _GC_DeviceInfo_h
#define _GC_DeviceInfo_h 1

#include <string>

struct DeviceInfo {
    std::string id;            // stable id for config, never a pointer
    std::string name;
    std::string transport;     // "ant", "ble", "virtual", "file"
    bool is_controllable_trainer = false;
    bool has_power = false;
    bool has_hr = false;
    bool has_speed_cadence = false;
};

#endif // _GC_DeviceInfo_h
