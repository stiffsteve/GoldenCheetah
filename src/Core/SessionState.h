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
// Session lifecycle state, shared by the train engine and every view.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
//
// The mapping from today's scattered state (RealtimeData trainer
// booleans and RealtimeController start/pause/stop return codes) to
// these values is a Phase 0 deliverable documented in
// doc/core-view-split/phase0-inventory.md, and is not defined here:
// this header only defines the vocabulary, so that mapping can be
// written and tested against it.

#ifndef _GC_SessionState_h
#define _GC_SessionState_h 1

enum class SessionState {
    Idle, Ready, Running, Paused, PausedWithError, Finished, Error
};

#endif // _GC_SessionState_h
