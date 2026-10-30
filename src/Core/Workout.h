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

// Phase 0 core type (core/view split, TDD section 7.2).
// A parsed workout, independent of file format, hardware, and GUI.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Workout parsing belongs in core: file in, Workout out. It needs no
// hardware and no GUI. (The parser itself is Phase 1 work; this header
// only defines the type it will produce.)
//
// Precedence, as in the TDD: target_watts takes precedence if > 0;
// target_pct_ftp is used if target_watts == 0; target_gradient_pct is
// used if the mode is Slope. %FTP resolution happens in WorkoutPlayer
// (Phase 1), using the FTP passed to its load() method — not here.

#ifndef _GC_Workout_h
#define _GC_Workout_h 1

#include <string>
#include <vector>

struct WorkoutStep {
    double duration_secs = 0;
    double target_watts = 0;        // absolute, takes precedence if > 0
    double target_pct_ftp = 0;      // used if target_watts == 0
    double target_gradient_pct = 0; // used if mode is Slope
    std::string label;
};

struct Workout {
    std::string name;
    std::string source_path; // .erg, .mrc, .zwo, etc.
    std::vector<WorkoutStep> steps;

    double total_duration_secs() const {
        double total = 0;
        for (const WorkoutStep &step : steps) {
            total += step.duration_secs;
        }
        return total;
    }
};

#endif // _GC_Workout_h
