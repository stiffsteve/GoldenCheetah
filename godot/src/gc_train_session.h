// Godot second client, step 2: GDExtension node wrapping the
// C ABI (GcSessionC.h). One TrainSession, created on ready,
// ticked from _process, polled each frame into GDScript-visible
// state. It talks to the session only through the C ABI — no
// C++ core types, no Qt, no internals. The game pulls the
// latest sample each frame (TDD 12.1); it does not block on Qt.
//
// fixed_step_ms: 0 (default) ticks with the real frame delta;
// a positive value ticks that many simulated ms per frame
// instead, which is what the headless proof uses (250 ms per
// frame, the same cadence as the console dashboard and the C
// harness, so the numbers are comparable).
//
// Rider params and workout loading are properties/methods so
// a real app can configure the session instead of taking the
// proof defaults: ftp_watts (250) and weight_kg (75) are read
// by _ready when it creates the session (they are creation
// time in the C ABI, so changing them after _ready needs a
// recreate — there is no mid-session setter), and
// auto_load_workout (true) controls whether _ready loads the
// built-in demo workout and starts it. With auto_load_workout
// off, the session is Idle after _ready and the scene loads a
// workout itself with load_workout_text() and starts it with
// start_session() — the flow a real app uses, shown in
// example.gd. Values set in a .tscn are applied before
// _ready, which is what makes the properties work for
// create-time params. The headless test keeps the defaults,
// so it pins the auto-load behaviour unchanged.
//
// device_kind (0, default) selects the trainer the session
// builds: 0 is the FakeDevice (scripted test double, what
// the test and the proofs use), 1 is the VirtualRider (the
// game device: app-driven watts via set_virtual_watts,
// speed from the physics model). _ready creates through
// gc_session_create_rider with the full profile; height_cm
// and bike_weight_kg are still literals here (175 / 8) and
// become properties in step 4 of the game plan.

#ifndef GC_TRAIN_SESSION_H
#define GC_TRAIN_SESSION_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>

typedef struct gc_session gc_session_t;

namespace godot {

class GcTrainSession : public Node {
    GDCLASS(GcTrainSession, Node)

public:
    GcTrainSession();
    ~GcTrainSession();

    void _ready() override;
    void _process(double delta) override;

    // GDScript-visible state, refreshed each _process.
    double get_watts() const { return watts_; }
    double get_watts_3s() const { return watts_3s_; }
    double get_heartrate() const { return heartrate_; }
    double get_cadence() const { return cadence_; }
    double get_speed_kph() const { return speed_kph_; }
    double get_distance_m() const { return distance_m_; }
    double get_altitude_m() const { return altitude_m_; }
    double get_gradient_pct() const { return gradient_pct_; }
    double get_target_watts() const { return target_watts_; }
    int get_step_index() const { return step_index_; }
    int get_state() const { return state_; }
    double get_time_in_step_secs() const { return time_in_step_secs_; }
    double get_time_remaining_secs() const {
        return time_remaining_secs_;
    }
    double get_normalized_power() const { return normalized_power_; }
    double get_wbal_joules() const { return wbal_joules_; }
    double get_tss_so_far() const { return tss_so_far_; }
    bool get_power_valid() const { return power_valid_; }
    bool get_hr_valid() const { return hr_valid_; }
    bool get_trainer_connected() const { return trainer_connected_; }
    double get_elapsed_secs() const { return elapsed_secs_; }

    void set_fixed_step_ms(int ms) { fixed_step_ms_ = ms; }
    int get_fixed_step_ms() const { return fixed_step_ms_; }

    void set_ftp_watts(double ftp) { ftp_watts_ = ftp; }
    double get_ftp_watts() const { return ftp_watts_; }
    void set_weight_kg(double weight) { weight_kg_ = weight; }
    double get_weight_kg() const { return weight_kg_; }
    void set_auto_load_workout(bool auto_load) {
        auto_load_workout_ = auto_load;
    }
    bool get_auto_load_workout() const { return auto_load_workout_; }
    void set_device_kind(int kind) { device_kind_ = kind; }
    int get_device_kind() const { return device_kind_; }

    // The game's watts input (virtual rider only): returns
    // the C ABI status, GC_ERR_STATE when the session was not
    // created with device_kind 1.
    int set_virtual_watts(double watts);

    // Load a workout (ERG/MRC text) and start the session.
    // Both return the C ABI status (0 is GC_OK): load fails
    // with GC_ERR_STATE unless the session is Idle, which is
    // why auto_load_workout must be off to load from a scene.
    int load_workout_text(const String &text, const String &source_path);
    int start_session();

    // Commands, forwarded to the session through the C ABI.
    void set_manual_erg(double target_watts);
    void clear_manual();
    void set_sim_gradient(double gradient_pct);
    void set_slope(double gradient_pct);
    void pause_session();
    void resume_session();
    void stop_session();

protected:
    static void _bind_methods();

private:
    void refresh();

    gc_session_t *session_ = nullptr;
    int fixed_step_ms_ = 0;
    double ftp_watts_ = 250.0;
    double weight_kg_ = 75.0;
    bool auto_load_workout_ = true;
    int device_kind_ = 0;
    double watts_ = 0;
    double watts_3s_ = 0;
    double heartrate_ = 0;
    double cadence_ = 0;
    double speed_kph_ = 0;
    double distance_m_ = 0;
    double altitude_m_ = 0;
    double gradient_pct_ = 0;
    double target_watts_ = 0;
    int step_index_ = -1;
    int last_step_index_ = -2;
    int state_ = 0;
    int last_state_ = -1;
    double time_in_step_secs_ = 0;
    double time_remaining_secs_ = 0;
    double normalized_power_ = 0;
    double wbal_joules_ = 0;
    double tss_so_far_ = 0;
    bool power_valid_ = false;
    bool hr_valid_ = false;
    bool trainer_connected_ = false;
    double elapsed_secs_ = 0;
};

} // namespace godot

#endif // GC_TRAIN_SESSION_H
