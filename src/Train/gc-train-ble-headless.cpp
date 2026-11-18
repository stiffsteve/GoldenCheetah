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

// Real-trainer hardware validation harness (core/view split).
// Drives the new core path (DeviceManager -> Bt40DeviceAdapter
// -> TrainSession) against a real FTMS trainer, with no GUI
// and no TrainSidebar switch. See
// doc/core-view-split/real-trainer-hardware-validation-plan.md
// for the test list, pass criteria, and the regression
// comparison against the legacy GUI path.
//
//   gc-train-ble-headless --scan [timeout_ms]
//       Scan only: print discovered FTMS trainers.
//   gc-train-ble-headless --device <id> [--workout file.erg]
//       [--ftp 250] [--weight 75] [--duration S] [--out run.csv]
//       [--sim-test] [--calibrate]
//       Full run. Default workout: 60 s at 150 W, 60 s at 250 W.
//   gc-train-ble-headless --fake [--out fake.csv]
//       Same run with a simulating fake backend: ERG convergence
//       toward the target, speed from the core physics model.
//       No radio, no scan. Tests the harness logic headlessly.
//
// CSV (1 Hz, --out or stdout): t_s,watts,target_watts,cadence,
// speed_kph,distance_m,state,power_valid,hr_valid,
// trainer_connected. State changes, step changes, and errors
// go to stderr as terse lines. Exit codes: 0 pass; 1 usage
// error; 2 device/scan/connect/no-samples/disconnect
// unrecoverable; 3 check failure (ERG convergence,
// calibration, sim assertion).
//
// The rider pedals throughout: a trainer cannot generate
// watts by itself. In ERG mode it sets resistance so the
// rider's power equals the target; in sim mode the resistance
// follows the gradient and the rider supplies the watts.
//
// Build (deliberately NOT wired into build.pro — a standalone
// program, like the other headless programs; the GUI build
// must run first, the harness links its objects):
//   export PATH="$HOME/Qt/6.8.3/gcc_64/bin:$PATH"
//   cd src/Train && qmake gc-train-ble-headless.pro && make
// Qt modules: core + bluetooth only (QProgressDialog is an
// unused include in BT40Controller.cpp — no widgets link).

#include "Core/CalibrationData.h"
#include "Core/Clock.h"
#include "Core/DeviceManager.h"
#include "Core/ICalibration.h"
#include "Core/PhysicsUtility.h"
#include "Core/TrainSession.h"
#include "Core/WorkoutParser.h"
#include "Bt40Backend.h"
#include "Bt40DeviceAdapter.h"
#include "Bt40Scanner.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char *stateName(SessionState state)
{
    switch (state) {
    case SessionState::Idle: return "Idle";
    case SessionState::Ready: return "Ready";
    case SessionState::Running: return "Running";
    case SessionState::Paused: return "Paused";
    case SessionState::PausedWithError: return "PausedWithError";
    case SessionState::Finished: return "Finished";
    case SessionState::Error: return "Error";
    }
    return "?";
}

struct Row {
    double t_s = 0;
    double watts = 0;
    double target_watts = 0;
    double cadence = 0;
    double speed_kph = 0;
    double distance_m = 0;
    SessionState state = SessionState::Idle;
    bool power_valid = false;
    bool hr_valid = false;
    bool trainer_connected = false;
};

// ISessionObserver that stores one row per second and reports
// state/step changes to stderr. t_s is seconds since
// session start (start_ms captured just before start()).
class CsvObserver : public ISessionObserver {
public:
    CsvObserver(int64_t start_ms, std::vector<Row> &rows)
        : start_ms_(start_ms), rows_(rows) {}

    void onTelemetry(const TelemetrySample &s) override
    {
        if (!rows_.empty() && s.monotonic_ms - last_row_ms_ < 1000) {
            return;
        }
        last_row_ms_ = s.monotonic_ms;
        Row r;
        r.t_s = (s.monotonic_ms - start_ms_) / 1000.0;
        r.watts = s.watts;
        r.target_watts = s.target_watts;
        r.cadence = s.cadence;
        r.speed_kph = s.speed_kph;
        r.distance_m = s.distance_m;
        r.state = state_;
        r.power_valid = s.power_valid;
        r.hr_valid = s.hr_valid;
        r.trainer_connected = s.trainer_connected;
        rows_.push_back(r);
    }
    void onSessionStateChanged(SessionState state) override
    {
        state_ = state;
        std::cerr << "state=" << stateName(state) << "\n";
    }
    void onWorkoutStepChanged(int index) override
    {
        std::cerr << "step=" << index << "\n";
    }

private:
    int64_t start_ms_;
    std::vector<Row> &rows_;
    int64_t last_row_ms_ = 0;
    SessionState state_ = SessionState::Idle;
};

// Simulating fake backend for --fake: the same IBt40Backend
// seam as the adapter suite's stub, but it *simulates* a
// trainer instead of recording calls — first-order ERG
// convergence toward the last target (tau 1000 ms, like
// FakeDevice), speed from the core physics model, cadence 90,
// HR 120. Calibration is a scripted progression: request ->
// STARTED (target 30 kph) -> SUCCESS with a 2500 ms spindown
// after 5 s. No radio, no event loop, deterministic enough
// to smoke-test the harness logic (arg parsing, session
// driving, CSV, exit codes) headlessly.
class FakeBt40Backend : public IBt40Backend {
public:
    explicit FakeBt40Backend(double weight_kg) : weight_kg_(weight_kg) {}

    bool connect() override
    {
        connected_ = true;
        last_update_ = std::chrono::steady_clock::now();
        return true;
    }
    void disconnect() override { connected_ = false; }
    bool isConnected() const override { return connected_; }

    Bt40Telemetry telemetry() override
    {
        auto now = std::chrono::steady_clock::now();
        double dt_s = std::chrono::duration<double>(now - last_update_).count();
        last_update_ = now;
        if (dt_s > 0 && dt_s < 5) {
            double k = 1.0 - std::exp(-dt_s / 1.0); // tau 1000 ms
            sim_watts_ += (target_watts_ - sim_watts_) * k;
        }
        Bt40Telemetry t;
        t.watts = sim_watts_;
        t.speed_kph = computeInstantSpeed(weight_kg_ + 8, last_gradient_,
                                          0, sim_watts_);
        t.cadence = sim_watts_ > 5 ? 90 : 0;
        t.heartrate = sim_watts_ > 5 ? 120 : 0;
        return t;
    }

    void setErg(double watts) override { target_watts_ = watts; }
    void setSlope(double gradient_pct, double) override
    {
        last_gradient_ = gradient_pct;
    }
    void setSim(double gradient_pct) override
    {
        last_gradient_ = gradient_pct;
    }
    // Resistance has no watt target; map the level onto one so
    // telemetry responds (fake only).
    void setResistance(double level_0_1) override
    {
        target_watts_ = level_0_1 * 300;
    }

    void requestCalibration() override
    {
        cal_requested_ = true;
        cal_request_time_ = std::chrono::steady_clock::now();
    }
    void resetCalibration() override
    {
        cal_requested_ = false;
        cal_state_ = CALIBRATION_STATE_IDLE;
    }
    uint8_t calibrationType() override
    {
        return CALIBRATION_TYPE_SPINDOWN;
    }
    uint8_t calibrationState() override
    {
        if (!cal_requested_) {
            return cal_state_;
        }
        double elapsed_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - cal_request_time_).count();
        if (elapsed_s >= 5) {
            cal_state_ = CALIBRATION_STATE_SUCCESS;
        } else {
            cal_state_ = CALIBRATION_STATE_STARTED;
        }
        return cal_state_;
    }
    double calibrationTargetSpeed() override { return 30.0; }
    uint16_t calibrationSpindownTime() override
    {
        return cal_state_ == CALIBRATION_STATE_SUCCESS ? 2500 : 0;
    }
    uint16_t calibrationZeroOffset() override { return 0; }
    uint16_t calibrationSlope() override { return 0; }

private:
    double weight_kg_;
    bool connected_ = false;
    double target_watts_ = 0;
    double sim_watts_ = 0;
    double last_gradient_ = 0;
    std::chrono::steady_clock::time_point last_update_ =
        std::chrono::steady_clock::now();
    bool cal_requested_ = false;
    std::chrono::steady_clock::time_point cal_request_time_ =
        std::chrono::steady_clock::now();
    uint8_t cal_state_ = CALIBRATION_STATE_IDLE;
};

struct Args {
    bool scan_only = false;
    bool fake = false;
    bool sim_test = false;
    bool calibrate = false;
    std::string device;
    std::string workout_path;
    std::string out_path;
    double ftp_watts = 250;
    double weight_kg = 75;
    int64_t duration_secs = 0; // 0 means the workout total
    int scan_timeout_ms = 10000;
};

bool parseArgs(int argc, char **argv, Args &args)
{
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                return "";
            }
            return argv[++i];
        };
        if (arg == "--scan") {
            args.scan_only = true;
        } else if (arg == "--fake") {
            args.fake = true;
        } else if (arg == "--sim-test") {
            args.sim_test = true;
        } else if (arg == "--calibrate") {
            args.calibrate = true;
        } else if (arg == "--device") {
            args.device = value();
        } else if (arg == "--workout") {
            args.workout_path = value();
        } else if (arg == "--out") {
            args.out_path = value();
        } else if (arg == "--ftp") {
            args.ftp_watts = std::stod(value());
        } else if (arg == "--weight") {
            args.weight_kg = std::stod(value());
        } else if (arg == "--duration") {
            args.duration_secs = std::stoll(value());
        } else if (arg == "--scan-timeout") {
            args.scan_timeout_ms = std::stoi(value());
        } else if (arg == "--help") {
            return false;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return false;
        }
    }
    return true;
}

void usage()
{
    std::cerr
        << "usage: gc-train-ble-headless --scan [--scan-timeout 10000]\n"
           "       gc-train-ble-headless [--device <id>] [--workout file.erg]\n"
           "           [--ftp 250] [--weight 75] [--duration S]\n"
           "           [--out run.csv] [--sim-test] [--calibrate]\n"
           "       gc-train-ble-headless --fake [same run flags]\n"
           "  <id> is ble:<address>, a raw address, or a name substring.\n";
}

std::string lowerOf(const std::string &s)
{
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

// Select one trainer from the manager's scan results.
// --device matches a full ble:<address> id, a raw address
// (case-insensitive), or a name substring (case-insensitive).
// No --device with exactly one trainer uses it; several with
// no --device is an error. Returns the id, or "" on failure
// (already reported).
std::string selectDevice(const DeviceManager &manager,
                         const std::string &want)
{
    std::vector<DeviceInfo> found = manager.scan();
    if (found.empty()) {
        std::cerr << "ERROR: no FTMS trainers found\n";
        return "";
    }
    if (want.empty()) {
        if (found.size() == 1) {
            return found[0].id;
        }
        std::cerr << "ERROR: " << found.size()
                  << " trainers found, --device is required:\n";
        for (const auto &info : found) {
            std::cerr << "  " << info.id << "  " << info.name << "\n";
        }
        return "";
    }
    std::string want_lower = lowerOf(want);
    std::vector<DeviceInfo> matches;
    for (const auto &info : found) {
        std::string id_lower = lowerOf(info.id);
        std::string addr_lower =
            id_lower.rfind("ble:", 0) == 0 ? id_lower.substr(4) : id_lower;
        if (id_lower == want_lower || addr_lower == want_lower ||
            lowerOf(info.name).find(want_lower) != std::string::npos) {
            matches.push_back(info);
        }
    }
    if (matches.empty()) {
        std::cerr << "ERROR: no trainer matches '" << want << "'\n";
        for (const auto &info : found) {
            std::cerr << "  " << info.id << "  " << info.name << "\n";
        }
        return "";
    }
    if (matches.size() > 1) {
        std::cerr << "ERROR: '" << want << "' matches "
                  << matches.size() << " trainers, be specific:\n";
        for (const auto &info : matches) {
            std::cerr << "  " << info.id << "  " << info.name << "\n";
        }
        return "";
    }
    return matches[0].id;
}

void writeCsv(std::ostream &out, const std::vector<Row> &rows)
{
    out << "t_s,watts,target_watts,cadence,speed_kph,distance_m,state,"
           "power_valid,hr_valid,trainer_connected\n";
    for (const Row &r : rows) {
        out << std::fixed << std::setprecision(1) << r.t_s << ","
            << std::setprecision(1) << r.watts << ","
            << std::setprecision(0) << r.target_watts << ","
            << std::setprecision(0) << r.cadence << ","
            << std::setprecision(1) << r.speed_kph << ","
            << std::setprecision(1) << r.distance_m << ","
            << stateName(r.state) << "," << (r.power_valid ? 1 : 0)
            << "," << (r.hr_valid ? 1 : 0) << ","
            << (r.trainer_connected ? 1 : 0) << "\n";
    }
    out << std::defaultfloat;
}

// ERG convergence check (plan §2d): for each distinct ERG
// target, the mean watts over the settled window — rows at
// least 15 s into the step — must be within max(±10 W, ±5%)
// of the target, and no settled sample more than ±25 W out.
// Returns false (with FAIL lines) on any step outside.
bool checkErg(const std::vector<Row> &rows)
{
    bool ok = true;
    size_t i = 0;
    while (i < rows.size()) {
        double target = rows[i].target_watts;
        if (target <= 0) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < rows.size() && rows[j].target_watts == target) {
            ++j;
        }
        // Segment [i, j): one step at this target.
        double step_start = rows[i].t_s;
        double sum = 0;
        int n = 0;
        double worst = 0;
        for (size_t k = i; k < j; ++k) {
            if (rows[k].t_s - step_start < 15) {
                continue; // settle window, excluded
            }
            if (!rows[k].power_valid) {
                continue;
            }
            sum += rows[k].watts;
            ++n;
            worst = std::max(worst, std::fabs(rows[k].watts - target));
        }
        if (n == 0) {
            std::cerr << "FAIL: step target " << target
                      << " W has no settled samples\n";
            ok = false;
        } else {
            double mean = sum / n;
            double band = std::max(10.0, 0.05 * target);
            std::cerr << "ERG step target " << std::fixed
                      << std::setprecision(0) << target
                      << " W: settled mean " << std::setprecision(1)
                      << mean << " W over " << n << " samples (band +/-"
                      << band << " W, worst sample off by " << worst
                      << " W)\n";
            if (std::fabs(mean - target) > band || worst > 25) {
                std::cerr << "FAIL: step target " << target
                          << " W outside the convergence band\n";
                ok = false;
            }
        }
        std::cerr << std::defaultfloat;
        i = j;
    }
    return ok;
}

// Sim-gradient check (plan §2e): mean speed in the 8% phase
// must be at least 15% lower than in the 0% phase, when the
// mean watts in the two phases are within 20% of each other.
// Returns 0 pass, 1 inconclusive (rider watts varied), 2 fail.
int checkSim(const std::vector<Row> &rows, double sim0_start_s)
{
    double w0 = 0, v0 = 0, w8 = 0, v8 = 0;
    int n0 = 0, n8 = 0;
    for (const Row &r : rows) {
        if (r.t_s < sim0_start_s || !r.power_valid) {
            continue;
        }
        if (r.t_s < sim0_start_s + 30) {
            w0 += r.watts;
            v0 += r.speed_kph;
            ++n0;
        } else if (r.t_s < sim0_start_s + 60) {
            w8 += r.watts;
            v8 += r.speed_kph;
            ++n8;
        }
    }
    if (n0 == 0 || n8 == 0) {
        std::cerr << "SIM: no samples in one phase, inconclusive\n";
        return 1;
    }
    w0 /= n0;
    v0 /= n0;
    w8 /= n8;
    v8 /= n8;
    std::cerr << "SIM: 0% phase mean " << std::fixed
              << std::setprecision(1) << w0 << " W, " << v0 << " kph; "
              << "8% phase mean " << w8 << " W, " << v8 << " kph\n";
    std::cerr << std::defaultfloat;
    if (std::fabs(w8 - w0) > 0.20 * std::max(w0, 1.0)) {
        std::cerr << "SIM: inconclusive, rider watts varied too much "
                     "between phases\n";
        return 1;
    }
    if (v0 > 0 && v8 < 0.85 * v0) {
        std::cerr << "SIM: PASS, speed fell "
                  << std::fixed << std::setprecision(0)
                  << (100 * (1 - v8 / v0)) << "% on the 8% gradient\n";
        std::cerr << std::defaultfloat;
        return 0;
    }
    std::cerr << "FAIL: SIM gradient had no effect on speed\n";
    return 2;
}

} // namespace

int main(int argc, char **argv)
{
    Args args;
    if (!parseArgs(argc, argv, args)) {
        usage();
        return 1;
    }

    QCoreApplication app(argc, argv);

    // --- Scan-only mode: list FTMS trainers, no session.
    if (args.scan_only) {
        QList<QBluetoothDeviceInfo> found =
            Bt40Scanner::discover(args.scan_timeout_ms);
        for (const auto &info : found) {
            DeviceInfo di = Bt40Scanner::deviceInfoFor(info);
            std::cout << di.id << "\t" << di.name << "\n";
        }
        if (found.empty()) {
            std::cerr << "no FTMS trainers found\n";
            return 2;
        }
        return 0;
    }

    // --- Workout: built-in default, or a file.
    Workout workout;
    if (args.workout_path.empty()) {
        workout.name = "ble-validation";
        workout.steps = {
            WorkoutStep{60, 150, 0, 0, "150W"},
            WorkoutStep{60, 250, 0, 0, "250W"},
        };
    } else {
        std::ifstream in(args.workout_path);
        if (!in) {
            std::cerr << "cannot open workout: " << args.workout_path
                      << "\n";
            return 1;
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        WorkoutParseResult parsed =
            parseWorkout(buffer.str(), args.workout_path);
        if (!parsed.ok) {
            std::cerr << "cannot parse workout: " << parsed.error
                      << "\n";
            return 1;
        }
        workout = parsed.workout;
    }
    int64_t workout_ms =
        static_cast<int64_t>(workout.total_duration_secs() * 1000);
    if (args.duration_secs > 0) {
        workout_ms = args.duration_secs * 1000;
    }
    int64_t total_ms = workout_ms + (args.sim_test ? 60000 : 0);

    // --- Device, through the real DeviceManager path.
    DeviceManager manager;
    std::string device_id;
    if (args.fake) {
        DeviceInfo info;
        info.id = "ble:00:00:00:00:00:00";
        info.name = "Fake Hub";
        info.transport = "ble";
        info.is_controllable_trainer = true;
        info.has_power = true;
        info.has_speed_cadence = true;
        double weight = args.weight_kg;
        manager.registerDevice(
            info, [info, weight](const DeviceInfo &) {
                return std::make_unique<Bt40DeviceAdapter>(
                    info, std::make_unique<FakeBt40Backend>(weight));
            });
        device_id = info.id;
    } else {
        int n = discoverAndRegisterBt40Devices(manager,
                                               args.scan_timeout_ms);
        if (n == 0) {
            std::cerr << "ERROR: no FTMS trainers found in scan\n";
            return 2;
        }
        device_id = selectDevice(manager, args.device);
        if (device_id.empty()) {
            return 2;
        }
    }
    std::unique_ptr<IDevice> device = manager.open(device_id);
    if (!device) {
        std::cerr << "ERROR: cannot open " << device_id << "\n";
        return 2;
    }
    Bt40DeviceAdapter *adapter =
        dynamic_cast<Bt40DeviceAdapter *>(device.get());
    if (adapter == nullptr) {
        std::cerr << "ERROR: opened device is not a Bt40DeviceAdapter\n";
        return 1;
    }

    // --- Session.
    SystemClock clock;
    std::vector<Row> rows;
    TrainSession session;
    session.setClock(&clock);
    AthleteProfile profile;
    profile.ftp_watts = args.ftp_watts;
    profile.weight_kg = args.weight_kg;
    session.setAthleteProfile(profile);
    session.loadWorkout(workout);
    int64_t start_ms = clock.nowMonotonicMs();
    CsvObserver observer(start_ms, rows);
    session.subscribe(&observer);
    session.addDevice(std::move(device));

    // Connect first (idempotent — start() calls it again), and
    // wait for the GATT link before starting the player, so the
    // workout does not run while the trainer is not driving.
    adapter->connect();
    bool linked = false;
    for (int waited = 0; waited < 30000; waited += 100) {
        app.processEvents(QEventLoop::AllEvents, 20);
        if (adapter->isConnected()) {
            linked = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!linked) {
        std::cerr << "ERROR: link did not come up within 30 s\n";
        session.stopAndSave();
        return 2;
    }
    std::cerr << "connected: " << device_id << "\n";

    session.start();
    start_ms = clock.nowMonotonicMs();

    // --- Real-time loop: 250 ms ticks, event loop pumped
    // between ticks so BLE notifications arrive.
    auto loop_start = std::chrono::steady_clock::now();
    int64_t last_tick_ms = -1;
    int64_t error_since_ms = -1;
    int64_t last_resume_ms = -1;
    int disconnects = 0;
    int sim_phase = -1; // -1 none, 0 at 0%, 1 at 8%
    int64_t sim_phase_start_ms = 0;
    double sim0_start_s = 0;
    bool have_sample = false;
    int exit_code = 0;

    while (true) {
        int64_t elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - loop_start)
                .count();
        if (elapsed_ms >= total_ms) {
            break;
        }
        app.processEvents(QEventLoop::AllEvents, 20);
        int64_t now = clock.nowMonotonicMs();
        if (last_tick_ms < 0 || now - last_tick_ms >= 250) {
            // Disconnect handling: PausedWithError -> retry
            // resume every 5 s; give up after 60 s.
            if (session.state() == SessionState::PausedWithError) {
                if (error_since_ms < 0) {
                    error_since_ms = now;
                    ++disconnects;
                    std::cerr << "ERROR: trainer disconnected, "
                                 "state=PausedWithError\n";
                }
                if (now - error_since_ms > 60000) {
                    std::cerr << "ERROR: no reconnect within 60 s\n";
                    exit_code = 2;
                    break;
                }
                if (last_resume_ms < 0 ||
                    now - last_resume_ms >= 5000) {
                    last_resume_ms = now;
                    session.resume();
                    if (session.state() == SessionState::Running) {
                        error_since_ms = -1;
                        std::cerr << "reconnected, session Running\n";
                    }
                }
            }
            adapter->tick(now);
            session.tick();
            last_tick_ms = now;

            // Sim phases run after the workout, while the
            // session is in free ride (no workout command).
            if (args.sim_test && sim_phase < 0 &&
                elapsed_ms >= workout_ms) {
                session.setSimGradient(0);
                sim_phase = 0;
                sim_phase_start_ms = elapsed_ms;
                sim0_start_s = (now - start_ms) / 1000.0;
                std::cerr << "sim phase 0%\n";
            }
            if (args.sim_test && sim_phase == 0 &&
                elapsed_ms - sim_phase_start_ms >= 30000) {
                session.setSimGradient(8);
                sim_phase = 1;
                sim_phase_start_ms = elapsed_ms;
                std::cerr << "sim phase 8%\n";
            }
        }
        if (!have_sample) {
            for (const Row &r : rows) {
                if (r.power_valid) {
                    have_sample = true;
                    break;
                }
            }
            if (!have_sample && elapsed_ms > 10000) {
                std::cerr << "ERROR: no power-valid sample within 10 s\n";
                exit_code = 2;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // --- Calibration phase (after the workout). Pause the
    // session first so it stops sending trainer commands
    // while the spin-down runs; keep ticking the adapter so
    // the CSV keeps flowing.
    int cal_result = 0; // 0 not run, 1 pass, 2 fail
    if (args.calibrate && exit_code == 0) {
        ICalibration *cal = adapter; // the adapter implements it
        session.pause();
        cal->resetCalibrationState();
        cal->requestCalibration();
        std::cerr << "calibration requested: pedal up to the target "
                     "speed, then coast\n";
        uint8_t last_state = cal->calibrationState();
        std::cerr << "calibration state=" << (int)last_state << "\n";
        auto cal_start = std::chrono::steady_clock::now();
        bool done = false;
        while (true) {
            double elapsed_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cal_start).count();
            if (elapsed_s > 120) {
                break;
            }
            app.processEvents(QEventLoop::AllEvents, 20);
            int64_t now = clock.nowMonotonicMs();
            if (last_tick_ms < 0 || now - last_tick_ms >= 250) {
                adapter->tick(now);
                session.tick();
                last_tick_ms = now;
            }
            uint8_t state = cal->calibrationState();
            if (state != last_state) {
                last_state = state;
                std::cerr << "calibration state=" << (int)state;
                if (state == CALIBRATION_STATE_STARTED) {
                    std::cerr << " target speed "
                              << cal->calibrationTargetSpeed()
                              << " kph: pedal to it, then coast";
                } else if (state == CALIBRATION_STATE_COAST) {
                    std::cerr << ": coasting";
                }
                std::cerr << "\n";
            }
            if (state == CALIBRATION_STATE_SUCCESS) {
                std::cerr << "calibration SUCCESS, spindown time "
                          << cal->calibrationSpindownTime() << " ms\n";
                cal_result = 1;
                done = true;
                break;
            }
            if (state == CALIBRATION_STATE_FAILURE ||
                state == CALIBRATION_STATE_FAILURE_SPINDOWN_TOO_SLOW ||
                state == CALIBRATION_STATE_FAILURE_SPINDOWN_TOO_FAST) {
                std::cerr << "calibration FAILURE, state="
                          << (int)state << "\n";
                cal_result = 2;
                done = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (!done) {
            std::cerr << "FAIL: calibration timed out after 120 s\n";
            cal_result = 2;
        }
    }

    session.stopAndSave();

    // --- Output and checks.
    if (!args.out_path.empty()) {
        std::ofstream out(args.out_path);
        if (!out) {
            std::cerr << "ERROR: cannot open output: " << args.out_path
                      << "\n";
            return 2;
        }
        writeCsv(out, rows);
        std::cerr << "wrote " << rows.size() << " rows to "
                  << args.out_path << "\n";
    } else {
        writeCsv(std::cout, rows);
    }

    if (exit_code != 0) {
        std::cerr << "FAIL\n";
        return exit_code;
    }
    if (!checkErg(rows)) {
        std::cerr << "FAIL\n";
        return 3;
    }
    if (args.sim_test) {
        int sim = checkSim(rows, sim0_start_s);
        if (sim == 2) {
            std::cerr << "FAIL\n";
            return 3;
        }
    }
    if (cal_result == 2) {
        std::cerr << "FAIL\n";
        return 3;
    }
    std::cerr << "recorded " << rows.size() << " samples, "
              << disconnects << " disconnect(s)\n";
    std::cerr << "PASS\n";
    return 0;
}
