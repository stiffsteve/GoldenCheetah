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

// Phase 1: WorkoutPlayer (core/view split, TDD section 7.6).
// A pure state machine for playing a Workout. No threads, no
// devices, no GUI, no global clock. Time comes in as a parameter
// (now_monotonic_ms, the same units as TelemetrySample.monotonic_ms
// and FakeClock::nowMonotonicMs()), so tests drive it with a
// FakeClock and never sleep, and production drives it from a
// TrainSession tick in a later phase.
//
// API follows TDD 7.6 exactly, including its key rule: every
// getter that depends on time takes now_monotonic_ms. A getter
// with no time parameter cannot be correct for a time-driven
// player, so there is none (state() is the one exception, and it
// is documented below).
//
// State model (internal, all in milliseconds):
//   elapsed_base_ms: elapsed time at the last state change.
//   anchor_ms:       the now value at the last state change while
//                    Running. While Paused, anchor is unused.
//   elapsed(now) = elapsed_base_ms + (Running ? now - anchor_ms : 0)
// Pausing freezes elapsed by folding the running interval into
// elapsed_base_ms; resuming sets a new anchor. Seeking and
// skipping set elapsed_base_ms directly. This model makes pause,
// resume, seek, and skip all exact, with no accumulated drift.
//
// Rules, pinned by the Phase 1 unit tests:
//  - load() resets to Idle, with the given workout and FTP. FTP
//    is the load-time FTP; %FTP steps resolve against it, never
//    against a file header or global settings.
//  - start(now) on a workout with steps goes Running; start on
//    an empty workout, or on no workout, goes Finished (there is
//    nothing to play, and tick must return Idle, not hang).
//  - pause(now) only affects a Running player; pause on any other
//    state is a no-op. resume(now) only affects a Paused player.
//  - tick(now) returns Idle when Idle, Paused, or Finished, and
//    transitions a Running player to Finished once elapsed
//    reaches the total duration. Tick never advances time itself;
//    time only moves because the caller passes a larger now.
//  - Step resolution: a step's target resolves as watts if
//    target_watts > 0, else percent of FTP if target_pct_ftp > 0,
//    else gradient if target_gradient_pct != 0 (Slope mode),
//    else Erg 0. This is the Workout precedence rule, applied
//    here and only here; views never compute targets.
//  - currentStepIndex(now) is -1 when there is no current step:
//    no workout loaded, Idle, or Finished. Otherwise it is the
//    index of the step containing elapsed time, where a step
//    covers [start, end) — at exactly a step boundary, the next
//    step is current.
//  - timeInStepSecs(now) is elapsed minus the current step's
//    start, or 0 when there is no current step. timeRemainingSecs
//    is total minus elapsed, clamped at 0. elapsedSecs is elapsed,
//    clamped to the total.
//  - state() takes no time parameter (TDD API) and returns the
//    stored state. It therefore reports Running until tick() or
//    another time-aware call observes the end; the time-aware
//    getters (currentStepIndex, tick, etc.) compute Finished from
//    elapsed >= total even before tick runs. Callers that need a
//    time-accurate state should use tick() or check
//    elapsedSecs(now) >= total, not state() alone after a long
//    gap. This asymmetry is inherent to the TDD API, not a bug
//    introduced here, and it is documented so Phase 4's
//    TrainSession does not rely on state() alone.
//  - seek(time_secs, now) and skipStep(now) clamp elapsed to
//    [0, total]. Seeking to or past the total finishes the
//    workout; skipping the last step finishes it. Seeking while
//    Paused stays Paused at the new position; seeking while
//    Running continues from the new position.
//  - All durations in WorkoutStep are seconds (double); all
//    player times are milliseconds (int64_t). Conversions use
//    double seconds * 1000, with no rounding to whole seconds,
//    so sub-second steps (e.g. 30 s ERG intervals are fine, and
//    0.5 s steps also work).
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other Phase 0/1 core types.

#ifndef _GC_WorkoutPlayer_h
#define _GC_WorkoutPlayer_h 1

#include "Workout.h"
#include "TrainerCommand.h"

#include <cstdint>

enum class WorkoutState { Idle, Running, Paused, Finished };

class WorkoutPlayer {
public:
    void load(const Workout &workout, double ftp_watts) {
        workout_ = workout;
        ftp_watts_ = ftp_watts;
        loaded_ = true;
        state_ = WorkoutState::Idle;
        elapsed_base_ms_ = 0;
        anchor_ms_ = 0;
    }

    void start(int64_t now_monotonic_ms) {
        if (!loaded_) {
            state_ = WorkoutState::Finished;
            return;
        }
        if (workout_.steps.empty() || totalDurationMs() == 0) {
            state_ = WorkoutState::Finished;
            elapsed_base_ms_ = 0;
            return;
        }
        // Starting always restarts from the beginning, even if a
        // previous run finished or was paused mid-workout. Callers
        // that want resume-after-pause must use resume(), not start().
        state_ = WorkoutState::Running;
        elapsed_base_ms_ = 0;
        anchor_ms_ = now_monotonic_ms;
    }

    void pause(int64_t now_monotonic_ms) {
        if (state_ != WorkoutState::Running) {
            return;
        }
        elapsed_base_ms_ = elapsedMs(now_monotonic_ms);
        state_ = WorkoutState::Paused;
    }

    void resume(int64_t now_monotonic_ms) {
        if (state_ != WorkoutState::Paused) {
            return;
        }
        state_ = WorkoutState::Running;
        anchor_ms_ = now_monotonic_ms;
    }

    void skipStep(int64_t now_monotonic_ms) {
        if (state_ != WorkoutState::Running &&
            state_ != WorkoutState::Paused) {
            return;
        }
        int index = currentStepIndex(now_monotonic_ms);
        if (index < 0) {
            // Already past the end (should not happen while Running
            // or Paused, but be safe): finish.
            finishAt(now_monotonic_ms);
            return;
        }
        // Elapsed at the end of the current step.
        double end_secs = 0;
        for (int i = 0; i <= index; ++i) {
            end_secs += workout_.steps[i].duration_secs;
        }
        setElapsedMs(static_cast<int64_t>(end_secs * 1000.0),
                     now_monotonic_ms);
    }

    void seek(double time_secs, int64_t now_monotonic_ms) {
        if (state_ != WorkoutState::Running &&
            state_ != WorkoutState::Paused) {
            return;
        }
        int64_t target_ms = static_cast<int64_t>(time_secs * 1000.0);
        if (target_ms < 0) {
            target_ms = 0;
        }
        if (target_ms > totalDurationMs()) {
            target_ms = totalDurationMs();
        }
        setElapsedMs(target_ms, now_monotonic_ms);
    }

    TrainerCommand tick(int64_t now_monotonic_ms) {
        if (state_ != WorkoutState::Running) {
            return TrainerCommand();
        }
        int64_t elapsed = elapsedMs(now_monotonic_ms);
        if (elapsed >= totalDurationMs()) {
            state_ = WorkoutState::Finished;
            elapsed_base_ms_ = totalDurationMs();
            return TrainerCommand();
        }
        // Command resolution lives in commandForElapsed(), shared
        // with currentTargetWatts(), so the two cannot diverge.
        return commandForElapsed(elapsed);
    }

    WorkoutState state() const { return state_; }
    bool hasWorkout() const { return loaded_; }

    int currentStepIndex(int64_t now_monotonic_ms) const {
        if (!loaded_ || workout_.steps.empty()) {
            return -1;
        }
        if (state_ == WorkoutState::Idle) {
            return -1;
        }
        int64_t elapsed = elapsedMs(now_monotonic_ms);
        if (elapsed >= totalDurationMs()) {
            return -1;
        }
        return stepIndexForElapsed(elapsed);
    }

    double timeInStepSecs(int64_t now_monotonic_ms) const {
        int index = currentStepIndex(now_monotonic_ms);
        if (index < 0) {
            return 0;
        }
        double start_secs = 0;
        for (int i = 0; i < index; ++i) {
            start_secs += workout_.steps[i].duration_secs;
        }
        return (elapsedMs(now_monotonic_ms) / 1000.0) - start_secs;
    }

    double timeRemainingSecs(int64_t now_monotonic_ms) const {
        if (!loaded_) {
            return 0;
        }
        double remaining =
            (totalDurationMs() - elapsedMs(now_monotonic_ms)) / 1000.0;
        return remaining > 0 ? remaining : 0;
    }

    double currentTargetWatts(int64_t now_monotonic_ms) const {
        TrainerCommand cmd = commandForElapsed(elapsedMs(now_monotonic_ms));
        return cmd.target_watts;
    }

    double elapsedSecs(int64_t now_monotonic_ms) const {
        if (!loaded_) {
            return 0;
        }
        int64_t elapsed = elapsedMs(now_monotonic_ms);
        if (elapsed > totalDurationMs()) {
            elapsed = totalDurationMs();
        }
        return elapsed / 1000.0;
    }

private:
    int64_t totalDurationMs() const {
        double total_secs = 0;
        for (const WorkoutStep &step : workout_.steps) {
            total_secs += step.duration_secs;
        }
        return static_cast<int64_t>(total_secs * 1000.0);
    }

    int64_t elapsedMs(int64_t now_monotonic_ms) const {
        if (state_ == WorkoutState::Running) {
            return elapsed_base_ms_ + (now_monotonic_ms - anchor_ms_);
        }
        return elapsed_base_ms_;
    }

    // Index of the step containing elapsed_ms, or -1 if past the end.
    int stepIndexForElapsed(int64_t elapsed_ms) const {
        double cursor_secs = 0;
        double elapsed_secs = elapsed_ms / 1000.0;
        for (int i = 0; i < static_cast<int>(workout_.steps.size()); ++i) {
            double end_secs = cursor_secs + workout_.steps[i].duration_secs;
            if (elapsed_secs < end_secs) {
                return i;
            }
            cursor_secs = end_secs;
        }
        return -1;
    }

    TrainerCommand commandForElapsed(int64_t elapsed_ms) const {
        if (!loaded_ || state_ == WorkoutState::Idle) {
            return TrainerCommand();
        }
        if (elapsed_ms >= totalDurationMs()) {
            return TrainerCommand();
        }
        int index = stepIndexForElapsed(elapsed_ms);
        if (index < 0) {
            return TrainerCommand();
        }
        const WorkoutStep &step = workout_.steps[index];
        TrainerCommand cmd;
        if (step.target_watts > 0) {
            cmd.mode = TrainerCommand::Mode::Erg;
            cmd.target_watts = step.target_watts;
        } else if (step.target_pct_ftp > 0) {
            cmd.mode = TrainerCommand::Mode::Erg;
            cmd.target_watts = step.target_pct_ftp / 100.0 * ftp_watts_;
        } else if (step.target_gradient_pct != 0) {
            cmd.mode = TrainerCommand::Mode::Slope;
            cmd.gradient_pct = step.target_gradient_pct;
        } else {
            cmd.mode = TrainerCommand::Mode::Erg;
            cmd.target_watts = 0;
        }
        return cmd;
    }

    void setElapsedMs(int64_t elapsed_ms, int64_t now_monotonic_ms) {
        elapsed_base_ms_ = elapsed_ms;
        if (elapsed_ms >= totalDurationMs()) {
            state_ = WorkoutState::Finished;
            elapsed_base_ms_ = totalDurationMs();
        } else if (state_ == WorkoutState::Running) {
            anchor_ms_ = now_monotonic_ms;
        }
        // While Paused, anchor stays unused; elapsed_base_ms_ is the
        // position, and resume() will set a fresh anchor.
    }

    void finishAt(int64_t /*now_monotonic_ms*/) {
        state_ = WorkoutState::Finished;
        elapsed_base_ms_ = totalDurationMs();
    }

    Workout workout_;
    double ftp_watts_ = 250;
    bool loaded_ = false;
    WorkoutState state_ = WorkoutState::Idle;
    int64_t elapsed_base_ms_ = 0;
    int64_t anchor_ms_ = 0;
};

#endif // _GC_WorkoutPlayer_h
