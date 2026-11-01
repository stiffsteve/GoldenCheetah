// Phase 4 headless harness (core/view split, TDD section 14.1).
// Runs a full workout with no window, no event loop, and no Qwt.
//
//   gc-train-headless --workout file.erg --ftp 280 --weight 75
//                     --duration 1800 --out out.csv
//
// The device is always a FakeDevice trainer (empty script, so
// it generates samples by converging to the ERG targets the
// session sends it). The clock is a FakeClock advancing 250 ms
// per loop iteration. The recorder is a CsvRecorder. This
// exercises WorkoutPlayer, TrainSession, FakeDevice, and
// Recorder, in that order, with no GUI code involved.
//
// Deliberately NOT wired into qmake: it is a standalone
// program, built manually with:
//   g++ -std=c++17 -I src src/Core/gc-train-headless.cpp
//       -o gc-train-headless
// and run from the repo root. Wiring it into the GUI build
// would make the GUI build depend on a CLI it does not ship.
//
// Output is CSV, not FIT. See the Phase 4 plan doc, decision
// 1: FIT conversion is a separate, gated IO step with
// legacy-output validation, and is not claimed here. The
// harness prints the recorded sample count and the average
// watts per workout step, so a run can be checked by eye
// without opening the CSV.

#include "Core/FakeDevice.h"
#include "Core/TrainSession.h"
#include "Core/WorkoutParser.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Args {
    std::string workout_path;
    std::string out_path = "out.csv";
    double ftp_watts = 250;
    double weight_kg = 75;
    int64_t duration_secs = 0; // 0 means the full workout
};

bool parseArgs(int argc, char **argv, Args &args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                return "";
            }
            return argv[++i];
        };
        if (arg == "--workout") {
            args.workout_path = value();
        } else if (arg == "--out") {
            args.out_path = value();
        } else if (arg == "--ftp") {
            args.ftp_watts = std::stod(value());
        } else if (arg == "--weight") {
            args.weight_kg = std::stod(value());
        } else if (arg == "--duration") {
            args.duration_secs = std::stoll(value());
        } else if (arg == "--help") {
            return false;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return false;
        }
    }
    return !args.workout_path.empty();
}

void usage() {
    std::cerr << "usage: gc-train-headless --workout file.erg "
                 "[--ftp 280] [--weight 75] [--duration 1800] "
                 "[--out out.csv]\n";
}

} // namespace

int main(int argc, char **argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        usage();
        return 1;
    }

    std::ifstream workout_file(args.workout_path);
    if (!workout_file) {
        std::cerr << "cannot open workout: " << args.workout_path << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << workout_file.rdbuf();
    WorkoutParseResult parsed = parseWorkout(buffer.str(), args.workout_path);
    if (!parsed.ok) {
        std::cerr << "cannot parse workout: " << parsed.error << "\n";
        return 1;
    }
    double total_secs = 0;
    for (const auto &step : parsed.workout.steps) {
        total_secs += step.duration_secs;
    }
    if (args.duration_secs <= 0) {
        args.duration_secs = static_cast<int64_t>(total_secs);
    }

    FakeClock clock;
    clock.setMonotonicMs(0);
    clock.setWallclockMs(1700000000000LL);

    TrainSession session;
    session.setClock(&clock);
    AthleteProfile profile;
    profile.ftp_watts = args.ftp_watts;
    profile.weight_kg = args.weight_kg;
    session.setAthleteProfile(profile);

    CsvRecorder recorder(args.out_path);
    session.setRecorder(&recorder);

    FakeDeviceConfig device_config;
    device_config.name = "headless-trainer";
    device_config.erg_tau_ms = 1000;
    auto fake_ptr = std::make_unique<FakeDevice>(device_config);
    FakeDevice *fake = fake_ptr.get();
    session.addDevice(std::move(fake_ptr));

    session.loadWorkout(parsed.workout);
    session.start();
    if (!recorder.ok()) {
        std::cerr << "cannot open output: " << args.out_path << "\n";
        return 1;
    }

    // 250 ms loop, device ticked before session tick, clock
    // advanced after both. No sleep, no event loop.
    int64_t elapsed_ms = 0;
    while (elapsed_ms < args.duration_secs * 1000) {
        fake->tick(clock.nowMonotonicMs());
        session.tick();
        clock.advanceMs(250);
        elapsed_ms += 250;
        if (session.player().state() == WorkoutState::Finished) {
            break;
        }
    }
    session.stopAndSave();

    // Per-step average watts from the recorded CSV, by
    // target_watts column (step boundaries are where the
    // target changes). Printed, not asserted: this is a
    // harness, not a test.
    std::ifstream out(args.out_path);
    std::string line;
    std::getline(out, line); // header
    int64_t samples = 0;
    double sum_watts = 0;
    double last_target = -1;
    double step_sum = 0;
    int64_t step_count = 0;
    int step_index = 0;
    while (std::getline(out, line)) {
        std::stringstream ss(line);
        std::string field;
        // Columns: monotonic, wallclock, watts, watts_3s, hr,
        // cadence, speed, distance, altitude, gradient, target, ...
        std::getline(ss, field, ','); // monotonic
        std::getline(ss, field, ','); // wallclock
        std::getline(ss, field, ','); // watts
        double watts = std::stod(field);
        std::getline(ss, field, ','); // watts_3s
        std::getline(ss, field, ','); // hr
        std::getline(ss, field, ','); // cadence
        std::getline(ss, field, ','); // speed
        std::getline(ss, field, ','); // distance
        std::getline(ss, field, ','); // altitude
        std::getline(ss, field, ','); // gradient
        std::getline(ss, field, ','); // target
        double target = std::stod(field);
        ++samples;
        sum_watts += watts;
        if (target != last_target) {
            if (step_count > 0) {
                std::cout << "step " << step_index << ": avg watts "
                          << (step_sum / step_count) << "\n";
                ++step_index;
            }
            last_target = target;
            step_sum = 0;
            step_count = 0;
        }
        step_sum += watts;
        ++step_count;
    }
    if (step_count > 0) {
        std::cout << "step " << step_index << ": avg watts "
                  << (step_sum / step_count) << "\n";
    }
    std::cout << "recorded " << samples << " samples to " << args.out_path
              << ", overall avg watts "
              << (samples > 0 ? sum_watts / samples : 0) << "\n";
    return 0;
}
