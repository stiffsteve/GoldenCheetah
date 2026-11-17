// Godot second client, step 1: C ABI implementation TU.
//
// GcSessionC.h declares the C API and defines its inline
// functions in C++. A plain C caller (gc-session-c-test.c)
// cannot instantiate those inline functions, so this TU
// exists only to include the header once, compiled as C++,
// which emits the symbols the C caller links against. It
// contains no logic of its own.

#include "Core/GcSessionC.h"

namespace {

// The header's functions are inline, so merely including it
// emits no symbols. Taking their addresses here forces one
// instantiation of each, which is what a plain C caller links
// against. This array is never read; it exists only to keep
// the linker from discarding the definitions.
void *gc_session_c_symbols[] = {
    reinterpret_cast<void *>(&gc_session_create),
    reinterpret_cast<void *>(&gc_session_destroy),
    reinterpret_cast<void *>(&gc_session_load_workout_text),
    reinterpret_cast<void *>(&gc_session_start),
    reinterpret_cast<void *>(&gc_session_pause),
    reinterpret_cast<void *>(&gc_session_resume),
    reinterpret_cast<void *>(&gc_session_stop_and_save),
    reinterpret_cast<void *>(&gc_session_tick),
    reinterpret_cast<void *>(&gc_session_poll_telemetry),
    reinterpret_cast<void *>(&gc_session_live_metrics),
    reinterpret_cast<void *>(&gc_session_state),
    reinterpret_cast<void *>(&gc_session_current_step),
    reinterpret_cast<void *>(&gc_session_target_watts),
    reinterpret_cast<void *>(&gc_session_time_in_step_secs),
    reinterpret_cast<void *>(&gc_session_time_remaining_secs),
    reinterpret_cast<void *>(&gc_session_set_manual_erg),
    reinterpret_cast<void *>(&gc_session_clear_manual),
    reinterpret_cast<void *>(&gc_session_set_sim_gradient),
    reinterpret_cast<void *>(&gc_session_set_slope),
};

} // namespace
