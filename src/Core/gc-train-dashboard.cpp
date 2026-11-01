// Phase 5 second-client proof (core/view split, TDD 14.2).
// A minimal console dashboard that subscribes to a TrainSession
// and sends it commands, through the public API only.
//
//   gc-train-dashboard --duration 120
//
// It runs a two-step workout (60 s at 250 W, 60 s at 150 W)
// with a FakeDevice trainer, a FakeClock advancing 250 ms
// per loop iteration, and an observer that prints, once per
// second, the time, watts, 3s watts, target, live metrics
// (NP, W'bal, TSS so far), and the current step. Halfway
// through the first step it sends a manual ERG command
// (100 W), then clears it, to prove a second client can
// send commands, not just listen. It reaches into no
// internals: no player, no recorder internals, no device
// internals beyond the public FakeDevice tick that the
// headless harness already uses, and no session private
// state — everything it prints comes from observer callbacks
// and from session.liveMetrics(), both public.
//
// This is the TDD 14.2 proof in form-neutral form. If this
// ~200-line client works, the seam is real: a second client
// does not need to reach into internals. It is NOT a QML
// dashboard and NOT a Godot plugin; that form choice is
// still the user's to make (see the Phase 5 plan doc), and
// this console proof does not pre-empt it.
//
// Deliberately NOT wired into qmake: a standalone program,
// built manually with:
//   g++ -std=c++17 -I src src/Core/gc-train-dashboard.cpp
//       -o gc-train-dashboard
// and run from the repo root. No window, no event loop,
// no Qwt, no Qt at all — it uses only the pure core API,
// which is itself the point: a second client need not be
// a Qt client to work.

#include "Core/FakeDevice.h"
#include "Core/TrainSession.h"

#include <iostream>
#include <string>

namespace {

class DashboardObserver : public ISessionObserver {
public:
    void onTelemetry(const TelemetrySample &sample) override {
        // Print once per second (samples arrive every 250 ms).
        if (sample.monotonic_ms % 1000 == 0) {
            std::cout << "t=" << (sample.monotonic_ms / 1000)
                      << "s watts=" << sample.watts
                      << " 3s=" << sample.watts_3s
                      << " target=" << sample.target_watts
                      << " power_valid=" << sample.power_valid << "\n";
        }
        last_ = sample;
    }
    void onSessionStateChanged(SessionState state) override {
        std::cout << "state=" << static_cast<int>(state) << "\n";
    }
    void onWorkoutStepChanged(int index) override {
        std::cout << "step=" << index << "\n";
    }
    TelemetrySample last_;
};

} // namespace

int main(int argc, char **argv) {
    int64_t duration_secs = 120;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) {
            duration_secs = std::stoll(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "usage: gc-train-dashboard [--duration 120]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 1;
        }
    }

    FakeClock clock;
    clock.setMonotonicMs(0);
    clock.setWallclockMs(1700000000000LL);

    TrainSession session;
    session.setClock(&clock);
    AthleteProfile profile;
    profile.ftp_watts = 250;
    profile.weight_kg = 75;
    session.setAthleteProfile(profile);
    session.setRecorder(nullptr);

    FakeDeviceConfig device_config;
    device_config.name = "dashboard-trainer";
    device_config.erg_tau_ms = 1000;
    auto fake_ptr = std::make_unique<FakeDevice>(device_config);
    FakeDevice *fake = fake_ptr.get();
    session.addDevice(std::move(fake_ptr));

    DashboardObserver observer;
    session.subscribe(&observer);

    Workout workout;
    workout.name = "dashboard-workout";
    workout.steps = {
        {60, 250, 0, 0, "step1"},
        {60, 150, 0, 0, "step2"},
    };
    session.loadWorkout(workout);
    session.start();

    // 250 ms loop, device ticked before session tick, clock
    // advanced after both. No sleep, no event loop.
    bool manual_sent = false;
    bool manual_cleared = false;
    for (int64_t elapsed_ms = 0; elapsed_ms < duration_secs * 1000;
         elapsed_ms += 250) {
        fake->tick(clock.nowMonotonicMs());
        session.tick();
        // At 30 s, send a manual ERG command (100 W) through
        // the public session API. At 40 s, clear it, and the
        // workout command (250 W) takes over again.
        if (!manual_sent && elapsed_ms >= 30000) {
            TrainerCommand manual;
            manual.mode = TrainerCommand::Mode::Erg;
            manual.target_watts = 100;
            session.setManualCommand(manual);
            std::cout << "manual ERG 100 W sent\n";
            manual_sent = true;
        }
        if (!manual_cleared && elapsed_ms >= 40000) {
            TrainerCommand idle;
            session.setManualCommand(idle);
            std::cout << "manual cleared, workout resumes\n";
            manual_cleared = true;
        }
        clock.advanceMs(250);
    }
    session.stopAndSave();

    LiveMetrics metrics = session.liveMetrics();
    std::cout << "final: NP=" << metrics.normalized_power
              << " Wbal=" << metrics.wbal_joules
              << " TSS=" << metrics.tss_so_far << "\n";
    return 0;
}
