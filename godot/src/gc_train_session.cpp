// Godot second client, step 2: GDExtension node wrapping the
// C ABI. See gc_train_session.h for the design. The workout is
// the Phase 5 dashboard workout (60 s at 250 W, 60 s at 150 W)
// as ERG text, parsed by the existing WorkoutParser through
// the C ABI — the plugin contains no workout parsing of its own.

#include "gc_train_session.h"

#include "Core/GcSessionC.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace {

// Same ERG text as the C harness (gc-session-c-test.c).
const char *kErgText =
    "[COURSE HEADER]\n"
    "MINUTES  WATTS\n"
    "[END COURSE HEADER]\n"
    "[COURSE DATA]\n"
    "0.0\t250\n"
    "1.0\t250\n"
    "1.0\t150\n"
    "2.0\t150\n";

} // namespace

GcTrainSession::GcTrainSession() {}
GcTrainSession::~GcTrainSession() {
    if (session_ != nullptr) {
        gc_session_destroy(session_);
        session_ = nullptr;
    }
}

void GcTrainSession::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_watts"), &GcTrainSession::get_watts);
    ClassDB::bind_method(D_METHOD("get_watts_3s"),
                         &GcTrainSession::get_watts_3s);
    ClassDB::bind_method(D_METHOD("get_heartrate"),
                         &GcTrainSession::get_heartrate);
    ClassDB::bind_method(D_METHOD("get_cadence"),
                         &GcTrainSession::get_cadence);
    ClassDB::bind_method(D_METHOD("get_speed_kph"),
                         &GcTrainSession::get_speed_kph);
    ClassDB::bind_method(D_METHOD("get_distance_m"),
                         &GcTrainSession::get_distance_m);
    ClassDB::bind_method(D_METHOD("get_altitude_m"),
                         &GcTrainSession::get_altitude_m);
    ClassDB::bind_method(D_METHOD("get_gradient_pct"),
                         &GcTrainSession::get_gradient_pct);
    ClassDB::bind_method(D_METHOD("get_target_watts"),
                         &GcTrainSession::get_target_watts);
    ClassDB::bind_method(D_METHOD("get_step_index"),
                         &GcTrainSession::get_step_index);
    ClassDB::bind_method(D_METHOD("get_state"), &GcTrainSession::get_state);
    ClassDB::bind_method(D_METHOD("get_time_in_step_secs"),
                         &GcTrainSession::get_time_in_step_secs);
    ClassDB::bind_method(D_METHOD("get_time_remaining_secs"),
                         &GcTrainSession::get_time_remaining_secs);
    ClassDB::bind_method(D_METHOD("get_normalized_power"),
                         &GcTrainSession::get_normalized_power);
    ClassDB::bind_method(D_METHOD("get_wbal_joules"),
                         &GcTrainSession::get_wbal_joules);
    ClassDB::bind_method(D_METHOD("get_tss_so_far"),
                         &GcTrainSession::get_tss_so_far);
    ClassDB::bind_method(D_METHOD("get_power_valid"),
                         &GcTrainSession::get_power_valid);
    ClassDB::bind_method(D_METHOD("get_hr_valid"),
                         &GcTrainSession::get_hr_valid);
    ClassDB::bind_method(D_METHOD("get_trainer_connected"),
                         &GcTrainSession::get_trainer_connected);
    ClassDB::bind_method(D_METHOD("get_elapsed_secs"),
                         &GcTrainSession::get_elapsed_secs);
    ClassDB::bind_method(D_METHOD("set_fixed_step_ms"),
                         &GcTrainSession::set_fixed_step_ms);
    ClassDB::bind_method(D_METHOD("get_fixed_step_ms"),
                         &GcTrainSession::get_fixed_step_ms);
    ClassDB::bind_method(D_METHOD("set_ftp_watts"),
                         &GcTrainSession::set_ftp_watts);
    ClassDB::bind_method(D_METHOD("get_ftp_watts"),
                         &GcTrainSession::get_ftp_watts);
    ClassDB::bind_method(D_METHOD("set_weight_kg"),
                         &GcTrainSession::set_weight_kg);
    ClassDB::bind_method(D_METHOD("get_weight_kg"),
                         &GcTrainSession::get_weight_kg);
    ClassDB::bind_method(D_METHOD("set_auto_load_workout"),
                         &GcTrainSession::set_auto_load_workout);
    ClassDB::bind_method(D_METHOD("get_auto_load_workout"),
                         &GcTrainSession::get_auto_load_workout);
    ClassDB::bind_method(D_METHOD("set_device_kind"),
                         &GcTrainSession::set_device_kind);
    ClassDB::bind_method(D_METHOD("get_device_kind"),
                         &GcTrainSession::get_device_kind);
    ClassDB::bind_method(D_METHOD("set_height_cm"),
                         &GcTrainSession::set_height_cm);
    ClassDB::bind_method(D_METHOD("get_height_cm"),
                         &GcTrainSession::get_height_cm);
    ClassDB::bind_method(D_METHOD("set_bike_weight_kg"),
                         &GcTrainSession::set_bike_weight_kg);
    ClassDB::bind_method(D_METHOD("get_bike_weight_kg"),
                         &GcTrainSession::get_bike_weight_kg);
    ClassDB::bind_method(D_METHOD("set_virtual_watts"),
                         &GcTrainSession::set_virtual_watts);
    ClassDB::bind_method(D_METHOD("load_workout_text"),
                         &GcTrainSession::load_workout_text);
    ClassDB::bind_method(D_METHOD("start_session"),
                         &GcTrainSession::start_session);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ftp_watts"),
                 "set_ftp_watts", "get_ftp_watts");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight_kg"),
                 "set_weight_kg", "get_weight_kg");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_load_workout"),
                 "set_auto_load_workout", "get_auto_load_workout");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "device_kind"),
                 "set_device_kind", "get_device_kind");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_cm"),
                 "set_height_cm", "get_height_cm");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bike_weight_kg"),
                 "set_bike_weight_kg", "get_bike_weight_kg");
    ClassDB::bind_method(D_METHOD("set_manual_erg"),
                         &GcTrainSession::set_manual_erg);
    ClassDB::bind_method(D_METHOD("clear_manual"),
                         &GcTrainSession::clear_manual);
    ClassDB::bind_method(D_METHOD("set_sim_gradient"),
                         &GcTrainSession::set_sim_gradient);
    ClassDB::bind_method(D_METHOD("set_slope"), &GcTrainSession::set_slope);
    ClassDB::bind_method(D_METHOD("pause_session"),
                         &GcTrainSession::pause_session);
    ClassDB::bind_method(D_METHOD("resume_session"),
                         &GcTrainSession::resume_session);
    ClassDB::bind_method(D_METHOD("stop_session"),
                         &GcTrainSession::stop_session);

    ADD_SIGNAL(MethodInfo("step_changed", PropertyInfo(Variant::INT, "index")));
    ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
    ADD_SIGNAL(MethodInfo("telemetry_updated"));
}

void GcTrainSession::_ready() {
    gc_rider_profile_t profile;
    profile.ftp_watts = ftp_watts_;
    profile.weight_kg = weight_kg_;
    profile.height_cm = height_cm_;
    profile.bike_weight_kg = bike_weight_kg_;
    gc_device_kind_t kind = device_kind_ == 1
                                ? GC_DEVICE_VIRTUAL_RIDER
                                : GC_DEVICE_FAKE;
    session_ = gc_session_create_rider(&profile, kind);
    if (session_ == nullptr) {
        return;
    }
    if (auto_load_workout_) {
        if (gc_session_load_workout_text(session_, kErgText,
                                         "test.erg") != GC_OK) {
            return;
        }
        gc_session_start(session_);
    }
    refresh();
}

int GcTrainSession::load_workout_text(const String &text,
                                      const String &source_path) {
    if (session_ == nullptr) {
        return GC_ERR_NULL;
    }
    CharString text_utf8 = text.utf8();
    CharString path_utf8 = source_path.utf8();
    return gc_session_load_workout_text(session_, text_utf8.get_data(),
                                        path_utf8.get_data());
}

int GcTrainSession::start_session() {
    if (session_ == nullptr) {
        return GC_ERR_NULL;
    }
    return gc_session_start(session_);
}

void GcTrainSession::_process(double delta) {
    if (session_ == nullptr) {
        return;
    }
    int64_t dt_ms = fixed_step_ms_ > 0
                        ? static_cast<int64_t>(fixed_step_ms_)
                        : static_cast<int64_t>(delta * 1000.0);
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    gc_session_tick(session_, dt_ms);
    elapsed_secs_ += static_cast<double>(dt_ms) / 1000.0;
    refresh();
    emit_signal("telemetry_updated");
}

void GcTrainSession::refresh() {
    gc_telemetry_t sample{};
    gc_session_poll_telemetry(session_, &sample);
    watts_ = sample.watts;
    watts_3s_ = sample.watts_3s;
    heartrate_ = sample.heartrate;
    cadence_ = sample.cadence;
    speed_kph_ = sample.speed_kph;
    distance_m_ = sample.distance_m;
    altitude_m_ = sample.altitude_m;
    gradient_pct_ = sample.gradient_pct;
    target_watts_ = sample.target_watts;
    power_valid_ = sample.power_valid != 0;
    hr_valid_ = sample.hr_valid != 0;
    trainer_connected_ = sample.trainer_connected != 0;

    step_index_ = gc_session_current_step(session_);
    state_ = static_cast<int>(gc_session_state(session_));
    time_in_step_secs_ = gc_session_time_in_step_secs(session_);
    time_remaining_secs_ = gc_session_time_remaining_secs(session_);

    gc_live_metrics_t metrics{};
    gc_session_live_metrics(session_, &metrics);
    normalized_power_ = metrics.normalized_power;
    wbal_joules_ = metrics.wbal_joules;
    tss_so_far_ = metrics.tss_so_far;

    if (step_index_ != last_step_index_) {
        last_step_index_ = step_index_;
        emit_signal("step_changed", step_index_);
    }
    if (state_ != last_state_) {
        last_state_ = state_;
        emit_signal("state_changed", state_);
    }
}

int GcTrainSession::set_virtual_watts(double watts) {
    if (session_ == nullptr) {
        return GC_ERR_NULL;
    }
    return gc_session_set_virtual_watts(session_, watts);
}

void GcTrainSession::set_manual_erg(double target_watts) {
    if (session_ != nullptr) {
        gc_session_set_manual_erg(session_, target_watts);
    }
}

void GcTrainSession::clear_manual() {
    if (session_ != nullptr) {
        gc_session_clear_manual(session_);
    }
}

void GcTrainSession::set_sim_gradient(double gradient_pct) {
    if (session_ != nullptr) {
        gc_session_set_sim_gradient(session_, gradient_pct);
    }
}

void GcTrainSession::set_slope(double gradient_pct) {
    if (session_ != nullptr) {
        gc_session_set_slope(session_, gradient_pct);
    }
}

void GcTrainSession::pause_session() {
    if (session_ != nullptr) {
        gc_session_pause(session_);
    }
}

void GcTrainSession::resume_session() {
    if (session_ != nullptr) {
        gc_session_resume(session_);
    }
}

void GcTrainSession::stop_session() {
    if (session_ != nullptr) {
        gc_session_stop_and_save(session_);
    }
}
