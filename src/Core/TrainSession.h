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

// Phase 4: TrainSession (core/view split, TDD section 7.7).
// "The orchestrator. The only class that knows about devices,
// player, recorder, and observers together."
//
// Naming deviation from TDD 7.7, deliberate and documented:
// TDD names the session observer ITelemetryObserver, with
// onTelemetry, onSessionStateChanged, and onWorkoutStepChanged.
// Phase 2 already shipped an ITelemetryObserver with only
// onTelemetry for the TelemetryBus, and changing it now would
// break the bus contract and its tests. This header therefore
// defines ISessionObserver with all three methods, and leaves
// the bus observer untouched. A future Qt adapter can implement
// either interface; core does not force the choice.
//
// Lifecycle (SessionState, Phase 0):
//   Idle -> Ready (loadWorkout) -> Running (start) ->
//   Paused (pause) -> Running (resume) -> Finished
//   (stopAndSave). Running -> PausedWithError when the trainer
//   disconnects mid-workout; resume() reconnects and resumes.
//   A session with no workout can still run (free ride, manual
//   or sim commands); loadWorkout is optional, not required.
//
// tick() does TDD 7.7's work in TDD 7.7's order, every time:
//   1. Merge latest samples per device into one TelemetrySample
//      (TelemetryMerge, Phase 2; stale timeout configurable,
//      default 3000 ms — the Phase 3/4 production value).
//   2. Decide command: manual override first, else WorkoutPlayer
//      while Running, else sim gradient, else Idle.
//   3. Send command to the first connected ITrainer, if any.
//   4. Compute live metrics from history (LiveMetrics, Phase 4).
//   5. Append to Recorder at 1 Hz (not every tick).
//   6. Publish to observers; fire onWorkoutStepChanged when the
//      step index changes, and onSessionStateChanged when the
//      state changes. Recording, live metrics, and rendering
//      all see the same merged sample, because they are all
//      computed from it in this order, in this function.
//
// Rules pinned by the Phase 4 tests:
//  - Ownership: the session owns devices added with addDevice
//    (TDD Q6: session owns devices passed to it). It sets
//    itself as their sink on add. It does NOT own the clock,
//    the recorder, or observers; the caller keeps those alive
//    for the whole session.
//  - start() connects and starts every device, starts the
//    player (if a workout is loaded), and starts the recorder
//    with the wall clock. If no device connects, start() still
//    runs the player and recorder (a session with no hardware
//    is a valid free-ride/workout simulation); the trainer-
//    connected flag in samples will simply be false.
//  - Paused ticks publish merged samples but do NOT record
//    and do NOT send trainer commands. A pause must not
//    pollute the recorded file with paused time, and must not
//    drive the trainer while the user is paused.
//  - PausedWithError: detected on tick when a device that was
//    connected is no longer connected AND that device is the
//    trainer (a sensor disconnect is a dropout, handled by
//    merging, not an error). The player is paused, the
//    recorder is kept open (so the file is safe), and resume()
//    reconnects the trainer and resumes the player. If the
//    trainer cannot reconnect on resume, the session stays
//    PausedWithError.
//  - Manual override persists until cleared: setManualCommand
//    with an Idle command clears it. A manual command beats a
//    workout command for as long as it is set (TDD 15.2
//    manual override test), it does not expire after one tick.
//  - Recording at 1 Hz: the first tick after start records
//    immediately, then one sample per second of monotonic time,
//    regardless of tick cadence (250 ms ticks give one recorded
//    sample per four ticks, not four per second).
//  - Step change: fired when currentStepIndex changes, using
//    the player's index at tick time. When the workout
//    finishes, the session state becomes Finished only on
//    stopAndSave; a finished player with a still-Running
//    session keeps ticking (free ride after the workout) until
//    the caller stops it. That separation is deliberate:
//    workout end is not session end.
//  - No tickDevices() method: tests keep a raw FakeDevice*
//    before moving it into the session (TDD 15.4) and tick it
//    themselves. Manual ticking stays the feature.
//  - No threads, no Qt timer, no global clock: the session
//    reads time only from its IClock. A multi-threaded caller
//    must serialize onSample() and tick() itself, exactly as
//    TelemetryBus documents (see the Phase 4 plan, decision 3).
//  - The merged sample's target_watts is the decided command's
//    target (for ERG commands), so the recorded file shows
//    what the workout asked for alongside what the athlete did.
//  - The merged sample's watts_3s is the live 3s power from
//    LiveMetrics, because devices never set watts_3s (Phase 0
//    rule) and the session is where it is computed.
//
// Intentionally pure standard C++: no Qt, no Widgets, no Qwt, no GUI.
// Header-only, like the other core types.

#ifndef _GC_TrainSession_h
#define _GC_TrainSession_h 1

#include "AthleteProfile.h"
#include "Clock.h"
#include "IDevice.h"
#include "LiveMetrics.h"
#include "Recorder.h"
#include "SessionState.h"
#include "TelemetryMerge.h"
#include "TelemetrySample.h"
#include "TrainerCommand.h"
#include "Workout.h"
#include "WorkoutPlayer.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ISessionObserver {
public:
    virtual ~ISessionObserver() = default;
    virtual void onTelemetry(const TelemetrySample &sample) = 0;
    virtual void onSessionStateChanged(SessionState state) = 0;
    virtual void onWorkoutStepChanged(int index) = 0;
};

class TrainSession : public ISampleSink {
public:
    void addDevice(std::unique_ptr<IDevice> device) {
        device->setSampleSink(this);
        devices_.push_back(std::move(device));
    }

    void setClock(IClock *clock) { clock_ = clock; }
    void setRecorder(IRecorder *recorder) { recorder_ = recorder; }
    void setAthleteProfile(const AthleteProfile &profile) {
        profile_ = profile;
    }
    void setStaleTimeoutMs(int64_t timeout_ms) {
        stale_timeout_ms_ = timeout_ms;
    }

    void loadWorkout(const Workout &workout) {
        player_.load(workout, profile_.ftp_watts);
        if (state_ == SessionState::Idle) {
            setState(SessionState::Ready);
        }
    }

    void start() {
        if (state_ == SessionState::Running) {
            return;
        }
        int64_t now = clock_->nowMonotonicMs();
        for (auto &device : devices_) {
            device->connect();
            device->start();
        }
        if (player_.hasWorkout()) {
            player_.start(now);
        }
        if (recorder_ != nullptr) {
            recorder_->start(clock_->nowWallclockMs());
        }
        last_record_ms_ = now - 1000; // first tick records immediately
        last_step_index_ = -2; // force step event on first tick if any
        setState(SessionState::Running);
    }

    void pause() {
        if (state_ != SessionState::Running) {
            return;
        }
        player_.pause(clock_->nowMonotonicMs());
        setState(SessionState::Paused);
    }

    void resume() {
        if (state_ != SessionState::Paused &&
            state_ != SessionState::PausedWithError) {
            return;
        }
        // Reconnect the trainer, if it was lost. If it still
        // cannot connect, stay PausedWithError.
        if (state_ == SessionState::PausedWithError) {
            ITrainer *trainer = findTrainer();
            if (trainer != nullptr && !trainer->isConnected()) {
                if (!trainer->connect()) {
                    return;
                }
                trainer->start();
            }
        }
        player_.resume(clock_->nowMonotonicMs());
        setState(SessionState::Running);
    }

    void stopAndSave() {
        if (state_ == SessionState::Finished) {
            return;
        }
        for (auto &device : devices_) {
            device->stop();
            device->disconnect();
        }
        if (recorder_ != nullptr) {
            recorder_->finish();
        }
        setState(SessionState::Finished);
    }

    void lap() {
        if (recorder_ != nullptr && state_ == SessionState::Running) {
            recorder_->lap(clock_->nowMonotonicMs());
        }
    }

    void setManualCommand(const TrainerCommand &cmd) {
        if (cmd.mode == TrainerCommand::Mode::Idle) {
            manual_active_ = false;
        } else {
            manual_active_ = true;
            manual_command_ = cmd;
        }
    }

    void setSimGradient(double gradient_pct) {
        sim_active_ = gradient_pct != 0;
        sim_gradient_ = gradient_pct;
    }

    void subscribe(ISessionObserver *observer) {
        for (auto *existing : observers_) {
            if (existing == observer) {
                return;
            }
        }
        observers_.push_back(observer);
    }

    void unsubscribe(ISessionObserver *observer) {
        for (auto it = observers_.begin(); it != observers_.end(); ++it) {
            if (*it == observer) {
                observers_.erase(it);
                return;
            }
        }
    }

    TelemetrySample latest() const { return latest_; }
    SessionState state() const { return state_; }
    LiveMetrics liveMetrics() const { return metrics_; }
    const WorkoutPlayer &player() const { return player_; }

    // The loop. Called by the caller (worker thread in
    // production, test loop in tests) on a fixed cadence.
    void tick() {
        int64_t now = clock_->nowMonotonicMs();

        // Disconnect detection: a trainer that was connected
        // and is no longer connected, while Running, is an error.
        if (state_ == SessionState::Running) {
            ITrainer *trainer = findTrainer();
            if (trainer != nullptr && trainer_was_connected_ &&
                !trainer->isConnected()) {
                player_.pause(now);
                setState(SessionState::PausedWithError);
            }
        }
        updateTrainerConnectedFlag();

        // 1. Merge latest per device.
        std::vector<SourceSample> sources;
        for (const auto &entry : latest_by_device_) {
            sources.push_back(SourceSample{entry.first, entry.second,
                                           entry_received_ms_.at(entry.first)});
        }
        TelemetrySample merged =
            mergeSources(sources, now, stale_timeout_ms_);
        merged.monotonic_ms = now;
        merged.wallclock_ms = clock_->nowWallclockMs();

        // 2. Decide command.
        TrainerCommand command;
        if (manual_active_) {
            command = manual_command_;
        } else if (state_ == SessionState::Running &&
                   player_.hasWorkout() &&
                   player_.state() == WorkoutState::Running) {
            command = player_.tick(now);
        } else if (sim_active_) {
            command.mode = TrainerCommand::Mode::Sim;
            command.gradient_pct = sim_gradient_;
        }
        merged.target_watts =
            command.mode == TrainerCommand::Mode::Erg ? command.target_watts
                                                      : 0;

        // 3. Send to trainer, unless paused (paused ticks must
        // not drive the trainer).
        if (state_ == SessionState::Running) {
            sendToTrainer(command);
        }

        // 4. Live metrics from history (including this sample).
        if (merged.power_valid) {
            power_history_.push_back(
                PowerHistoryEntry{now, merged.watts, true});
        } else {
            power_history_.push_back(
                PowerHistoryEntry{now, merged.watts, false});
        }
        metrics_ = computeLiveMetrics(power_history_, now, profile_);
        merged.watts_3s = metrics_.watts_3s;

        // 5. Record at 1 Hz, only while Running.
        if (state_ == SessionState::Running && recorder_ != nullptr &&
            now - last_record_ms_ >= 1000) {
            recorder_->onSample(merged);
            last_record_ms_ = now;
        }

        // 6. Publish and step-change events.
        latest_ = merged;
        for (auto *observer : observers_) {
            observer->onTelemetry(merged);
        }
        int step_index = player_.hasWorkout()
                             ? player_.currentStepIndex(now)
                             : -1;
        if (step_index != last_step_index_) {
            last_step_index_ = step_index;
            for (auto *observer : observers_) {
                observer->onWorkoutStepChanged(step_index);
            }
        }
    }

    // ISampleSink, called by devices.
    void onSample(const std::string &device_id,
                  const TelemetrySample &sample) override {
        latest_by_device_[device_id] = sample;
        entry_received_ms_[device_id] = clock_->nowMonotonicMs();
    }

private:
    ITrainer *findTrainer() {
        for (auto &device : devices_) {
            if (auto *trainer = dynamic_cast<ITrainer *>(device.get())) {
                return trainer;
            }
        }
        return nullptr;
    }

    void updateTrainerConnectedFlag() {
        ITrainer *trainer = findTrainer();
        trainer_was_connected_ =
            trainer != nullptr && trainer->isConnected();
    }

    void sendToTrainer(const TrainerCommand &command) {
        ITrainer *trainer = findTrainer();
        if (trainer == nullptr || !trainer->isConnected()) {
            return;
        }
        switch (command.mode) {
        case TrainerCommand::Mode::Erg:
            trainer->setErgMode(command.target_watts);
            break;
        case TrainerCommand::Mode::Slope:
            trainer->setSlopeMode(command.gradient_pct, profile_.weight_kg);
            break;
        case TrainerCommand::Mode::Resistance:
            trainer->setResistanceMode(command.resistance_0_100);
            break;
        case TrainerCommand::Mode::Sim:
            trainer->setSimMode(command.gradient_pct, 0);
            break;
        case TrainerCommand::Mode::Idle:
            break;
        }
    }

    void setState(SessionState new_state) {
        if (state_ == new_state) {
            return;
        }
        state_ = new_state;
        for (auto *observer : observers_) {
            observer->onSessionStateChanged(new_state);
        }
    }

    std::vector<std::unique_ptr<IDevice>> devices_;
    IClock *clock_ = &system_clock_;
    IRecorder *recorder_ = nullptr;
    AthleteProfile profile_;
    WorkoutPlayer player_;
    int64_t stale_timeout_ms_ = 3000;
    SessionState state_ = SessionState::Idle;
    std::vector<ISessionObserver *> observers_;
    // Latest sample per device id, plus when it arrived.
    std::map<std::string, TelemetrySample> latest_by_device_;
    std::map<std::string, int64_t> entry_received_ms_;
    TelemetrySample latest_;
    LiveMetrics metrics_;
    std::vector<PowerHistoryEntry> power_history_;
    int64_t last_record_ms_ = 0;
    int last_step_index_ = -2;
    bool manual_active_ = false;
    TrainerCommand manual_command_;
    bool sim_active_ = false;
    double sim_gradient_ = 0;
    bool trainer_was_connected_ = false;
    SystemClock system_clock_;
};

#endif // _GC_TrainSession_h
