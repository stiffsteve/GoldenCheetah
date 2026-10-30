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

// Phase 0 core type (core/view split, TDD section 7.3).
// Time is injectable. Production uses a system clock; tests use a
// fake clock.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// No core or train class may call a global clock or a UI timer for
// control timing. It must receive an IClock*.
//
// FakeClock extension beyond the TDD: setWallclockMs() is provided
// alongside the TDD's setMonotonicMs(), so tests can set the two
// clocks independently. advanceMs() advances both by the same delta,
// preserving whatever offset was set.

#ifndef _GC_Clock_h
#define _GC_Clock_h 1

#include <chrono>
#include <cstdint>

class IClock {
public:
    virtual ~IClock() = default;
    virtual int64_t nowMonotonicMs() const = 0;
    virtual int64_t nowWallclockMs() const = 0;
};

class SystemClock : public IClock {
public:
    int64_t nowMonotonicMs() const override {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
    }
    int64_t nowWallclockMs() const override {
        return static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
    }
};

class FakeClock : public IClock {
public:
    int64_t nowMonotonicMs() const override { return monotonic_ms_; }
    int64_t nowWallclockMs() const override { return wallclock_ms_; }
    void advanceMs(int64_t delta_ms) {
        monotonic_ms_ += delta_ms;
        wallclock_ms_ += delta_ms;
    }
    void setMonotonicMs(int64_t v) { monotonic_ms_ = v; }
    void setWallclockMs(int64_t v) { wallclock_ms_ = v; }

private:
    int64_t monotonic_ms_ = 0;
    int64_t wallclock_ms_ = 0;
};

#endif // _GC_Clock_h
