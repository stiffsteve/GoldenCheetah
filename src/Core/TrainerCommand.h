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
// What the train engine tells a trainer to do, as a plain value.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// This replaces, in later phases, the scattered RealtimeController
// virtual setters (setLoad, setGradient, setMode, ...) with one
// explicit command. Views send intents; they do not call hardware
// directly, and this struct is the value those intents resolve to.

#ifndef _GC_TrainerCommand_h
#define _GC_TrainerCommand_h 1

struct TrainerCommand {
    enum class Mode { Idle, Erg, Slope, Resistance, Sim } mode = Mode::Idle;
    double target_watts = 0;
    double gradient_pct = 0;
    int resistance_0_100 = 0;
};

#endif // _GC_TrainerCommand_h
