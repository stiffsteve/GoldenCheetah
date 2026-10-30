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

// Phase 0 core type (core/view split, TDD section 7.4).
// The settings the train engine needs, passed explicitly.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// This replaces hidden reads of global settings inside engine code.
// The GUI layer is responsible for building this struct from stored
// settings and passing it in.
//
// Note: in v3.8, FTP lives per Zones::ZoneRange (see
// doc/core-view-split/phase0-inventory.md), not as a single number.
// Which range and sport the GUI layer uses to fill ftp_watts is an
// explicit adapter decision, recorded in Phase 0 step 4/5, not
// something this struct can decide on its own.

#ifndef _GC_AthleteProfile_h
#define _GC_AthleteProfile_h 1

struct AthleteProfile {
    double ftp_watts = 250;
    double weight_kg = 75;
    double bike_weight_kg = 8;
    double height_cm = 175;
};

#endif // _GC_AthleteProfile_h
