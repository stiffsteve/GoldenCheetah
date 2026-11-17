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

// Godot second client, step 1: stable C ABI wrapper (core/view
// split, TDD Q3 and section 12.1).
//
// Q3 accepted a stable C API for daemon and game clients, C++
// internally. Phases 0-5 built no such wrapper, so a Godot
// plugin had nothing to call: TrainSession is C++, header-only,
// owns devices in unique_ptr, and speaks ISessionObserver.
// This header is that wrapper, and it is the only thing a
// non-C++ client needs.
//
// Design, pinned here so a future client cannot drift from it:
//  - Opaque handle. `gc_session_t` is an incomplete type in C;
//    the caller never sees its fields, its size, or any C++
//    type. All state lives behind the handle.
//  - Plain C structs cross the boundary. Telemetry and live
//    metrics are fixed-layout structs of doubles, int64, and
//    ints (validity flags are ints, not C++ bool, so the ABI
//    does not depend on a compiler's bool size). No strings,
//    no pointers to C++ objects, no exceptions cross.
//  - Error codes, never crashes on a bad handle. Every
//    function null-checks its handle and returns gc_status.
//    GC_ERR_NULL is the answer to a null handle, not a
//    segfault. A destroyed handle must not be used again;
//    the wrapper cannot detect that, and says so here.
//  - One FakeDevice trainer, owned by the session. The
//    wrapper creates it, the session owns it (TDD Q6), and
//    the wrapper keeps a raw pointer only to tick it.
//    Real-device support is gated work, not this client.
//  - One tick drives everything. gc_session_tick(dt_ms)
//    advances the FakeClock, ticks the FakeDevice, then ticks
//    the session, in that order — the same order the console
//    dashboard uses. A game client calls tick once per frame
//    with the frame delta and polls afterwards; it never
//    ticks a device itself (TrainSession has no tickDevices,
//    by design, Phase 4).
//  - Single-threaded, caller-serialised. Same rule as
//    TrainSession: no threads in core. The caller calls
//    tick and poll on one thread.
//  - Workout loading is ERG/MRC text through the existing
//    WorkoutParser (Phase 1). CRS and other formats are
//    parse errors, exactly as in the parser. Loading after
//    start() is a state error.
//  - Recorder is NullRecorder. Recording to file is the
//    gated FIT step, not this client; the dashboard proof
//    used the same recorder.
//
// Intentionally pure standard C++ inside, C at the boundary:
// no Qt, no Widgets, no Qwt, no GUI. Header-only, like the
// other core types. Added to util/check-core-includes.sh.

#ifndef _GC_GcSessionC_h
#define _GC_GcSessionC_h 1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gc_session gc_session_t;

typedef enum gc_status {
    GC_OK = 0,
    GC_ERR_NULL = 1,
    GC_ERR_STATE = 2,
    GC_ERR_PARSE = 3,
    GC_ERR_NO_WORKOUT = 4
} gc_status_t;

// Mirrors SessionState order (Idle, Ready, Running, Paused,
// PausedWithError, Finished, Error). Do not reorder without
// updating the mapping in the implementation below.
typedef enum gc_state {
    GC_STATE_IDLE = 0,
    GC_STATE_READY = 1,
    GC_STATE_RUNNING = 2,
    GC_STATE_PAUSED = 3,
    GC_STATE_PAUSED_WITH_ERROR = 4,
    GC_STATE_FINISHED = 5,
    GC_STATE_ERROR = 6
} gc_state_t;

// Plain-C copy of TelemetrySample. Validity flags are ints:
// 1 means valid/connected, 0 means not. A dropout is
// power_valid == 0 with watts holding its last value, never
// zeroed — same rule as TelemetryMerge (Phase 2).
typedef struct gc_telemetry {
    int64_t monotonic_ms;
    int64_t wallclock_ms;
    double watts;
    double watts_3s;
    double heartrate;
    double cadence;
    double speed_kph;
    double distance_m;
    double altitude_m;
    double gradient_pct;
    double target_watts;
    int power_valid;
    int hr_valid;
    int trainer_connected;
    int trainer_load_pct;
} gc_telemetry_t;

typedef struct gc_live_metrics {
    double watts_3s;
    double normalized_power;
    double wbal_joules;
    double tss_so_far;
} gc_live_metrics_t;

gc_session_t *gc_session_create(double ftp_watts, double weight_kg);
void gc_session_destroy(gc_session_t *session);

gc_status_t gc_session_load_workout_text(gc_session_t *session,
                                         const char *text,
                                         const char *source_path);
gc_status_t gc_session_start(gc_session_t *session);
gc_status_t gc_session_pause(gc_session_t *session);
gc_status_t gc_session_resume(gc_session_t *session);
gc_status_t gc_session_stop_and_save(gc_session_t *session);

// Advance dt_ms (>= 0), tick device then session. Returns
// GC_ERR_STATE if dt_ms < 0.
gc_status_t gc_session_tick(gc_session_t *session, int64_t dt_ms);

gc_status_t gc_session_poll_telemetry(gc_session_t *session,
                                      gc_telemetry_t *out);
gc_status_t gc_session_live_metrics(gc_session_t *session,
                                    gc_live_metrics_t *out);
gc_state_t gc_session_state(gc_session_t *session);
int gc_session_current_step(gc_session_t *session);
double gc_session_target_watts(gc_session_t *session);
double gc_session_time_in_step_secs(gc_session_t *session);
double gc_session_time_remaining_secs(gc_session_t *session);

gc_status_t gc_session_set_manual_erg(gc_session_t *session,
                                      double target_watts);
gc_status_t gc_session_clear_manual(gc_session_t *session);
gc_status_t gc_session_set_sim_gradient(gc_session_t *session,
                                        double gradient_pct);
gc_status_t gc_session_set_slope(gc_session_t *session,
                                 double gradient_pct);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus

#include "Core/FakeDevice.h"
#include "Core/Recorder.h"
#include "Core/TrainSession.h"
#include "Core/WorkoutParser.h"

#include <memory>
#include <string>

namespace gc_session_c_detail {

struct GcSession {
    FakeClock clock;
    TrainSession session;
    NullRecorder recorder;
    FakeDevice *fake = nullptr;

    GcSession(double ftp_watts, double weight_kg) {
        clock.setMonotonicMs(0);
        clock.setWallclockMs(1700000000000LL);
        session.setClock(&clock);
        AthleteProfile profile;
        profile.ftp_watts = ftp_watts;
        profile.weight_kg = weight_kg;
        session.setAthleteProfile(profile);
        session.setRecorder(&recorder);
        FakeDeviceConfig config;
        config.name = "godot-trainer";
        config.erg_tau_ms = 1000;
        auto device = std::make_unique<FakeDevice>(config);
        fake = device.get();
        session.addDevice(std::move(device));
    }
};

inline gc_state_t toGcState(SessionState state) {
    switch (state) {
    case SessionState::Idle:
        return GC_STATE_IDLE;
    case SessionState::Ready:
        return GC_STATE_READY;
    case SessionState::Running:
        return GC_STATE_RUNNING;
    case SessionState::Paused:
        return GC_STATE_PAUSED;
    case SessionState::PausedWithError:
        return GC_STATE_PAUSED_WITH_ERROR;
    case SessionState::Finished:
        return GC_STATE_FINISHED;
    case SessionState::Error:
        return GC_STATE_ERROR;
    }
    return GC_STATE_ERROR;
}

inline void fillTelemetry(const TelemetrySample &sample,
                          gc_telemetry_t *out) {
    out->monotonic_ms = sample.monotonic_ms;
    out->wallclock_ms = sample.wallclock_ms;
    out->watts = sample.watts;
    out->watts_3s = sample.watts_3s;
    out->heartrate = sample.heartrate;
    out->cadence = sample.cadence;
    out->speed_kph = sample.speed_kph;
    out->distance_m = sample.distance_m;
    out->altitude_m = sample.altitude_m;
    out->gradient_pct = sample.gradient_pct;
    out->target_watts = sample.target_watts;
    out->power_valid = sample.power_valid ? 1 : 0;
    out->hr_valid = sample.hr_valid ? 1 : 0;
    out->trainer_connected = sample.trainer_connected ? 1 : 0;
    out->trainer_load_pct = sample.trainer_load_pct;
}

} // namespace gc_session_c_detail

extern "C" {

inline gc_session_t *gc_session_create(double ftp_watts,
                                       double weight_kg) {
    return reinterpret_cast<gc_session_t *>(
        new gc_session_c_detail::GcSession(ftp_watts, weight_kg));
}

inline void gc_session_destroy(gc_session_t *session) {
    if (session == nullptr) {
        return;
    }
    delete reinterpret_cast<gc_session_c_detail::GcSession *>(session);
}

inline gc_status_t gc_session_load_workout_text(gc_session_t *session,
                                                const char *text,
                                                const char *source_path) {
    if (session == nullptr || text == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    if (impl->session.state() != SessionState::Idle) {
        return GC_ERR_STATE;
    }
    WorkoutParseResult parsed = parseWorkout(
        std::string(text),
        source_path != nullptr ? std::string(source_path) : "");
    if (!parsed.ok) {
        return GC_ERR_PARSE;
    }
    impl->session.loadWorkout(parsed.workout);
    return GC_OK;
}

inline gc_status_t gc_session_start(gc_session_t *session) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->session.start();
    return GC_OK;
}

inline gc_status_t gc_session_pause(gc_session_t *session) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->session.pause();
    return GC_OK;
}

inline gc_status_t gc_session_resume(gc_session_t *session) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->session.resume();
    return GC_OK;
}

inline gc_status_t gc_session_stop_and_save(gc_session_t *session) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->session.stopAndSave();
    return GC_OK;
}

inline gc_status_t gc_session_tick(gc_session_t *session,
                                   int64_t dt_ms) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    if (dt_ms < 0) {
        return GC_ERR_STATE;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->clock.advanceMs(dt_ms);
    impl->fake->tick(impl->clock.nowMonotonicMs());
    impl->session.tick();
    return GC_OK;
}

inline gc_status_t gc_session_poll_telemetry(gc_session_t *session,
                                             gc_telemetry_t *out) {
    if (session == nullptr || out == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    gc_session_c_detail::fillTelemetry(impl->session.latest(), out);
    return GC_OK;
}

inline gc_status_t gc_session_live_metrics(gc_session_t *session,
                                           gc_live_metrics_t *out) {
    if (session == nullptr || out == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    LiveMetrics metrics = impl->session.liveMetrics();
    out->watts_3s = metrics.watts_3s;
    out->normalized_power = metrics.normalized_power;
    out->wbal_joules = metrics.wbal_joules;
    out->tss_so_far = metrics.tss_so_far;
    return GC_OK;
}

inline gc_state_t gc_session_state(gc_session_t *session) {
    if (session == nullptr) {
        return GC_STATE_ERROR;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    return gc_session_c_detail::toGcState(impl->session.state());
}

inline int gc_session_current_step(gc_session_t *session) {
    if (session == nullptr) {
        return -1;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    return impl->session.player().currentStepIndex(
        impl->clock.nowMonotonicMs());
}

inline double gc_session_target_watts(gc_session_t *session) {
    if (session == nullptr) {
        return 0;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    return impl->session.latest().target_watts;
}

inline double gc_session_time_in_step_secs(gc_session_t *session) {
    if (session == nullptr) {
        return 0;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    return impl->session.player().timeInStepSecs(
        impl->clock.nowMonotonicMs());
}

inline double gc_session_time_remaining_secs(gc_session_t *session) {
    if (session == nullptr) {
        return 0;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    return impl->session.player().timeRemainingSecs(
        impl->clock.nowMonotonicMs());
}

inline gc_status_t gc_session_set_manual_erg(gc_session_t *session,
                                             double target_watts) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    TrainerCommand manual;
    manual.mode = TrainerCommand::Mode::Erg;
    manual.target_watts = target_watts;
    impl->session.setManualCommand(manual);
    return GC_OK;
}

inline gc_status_t gc_session_clear_manual(gc_session_t *session) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    TrainerCommand idle;
    impl->session.setManualCommand(idle);
    return GC_OK;
}

inline gc_status_t gc_session_set_sim_gradient(gc_session_t *session,
                                               double gradient_pct) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    impl->session.setSimGradient(gradient_pct);
    return GC_OK;
}

inline gc_status_t gc_session_set_slope(gc_session_t *session,
                                        double gradient_pct) {
    if (session == nullptr) {
        return GC_ERR_NULL;
    }
    auto *impl =
        reinterpret_cast<gc_session_c_detail::GcSession *>(session);
    TrainerCommand slope;
    slope.mode = TrainerCommand::Mode::Slope;
    slope.gradient_pct = gradient_pct;
    impl->session.setManualCommand(slope);
    return GC_OK;
}

} // extern "C"

#endif // __cplusplus

#endif // _GC_GcSessionC_h
