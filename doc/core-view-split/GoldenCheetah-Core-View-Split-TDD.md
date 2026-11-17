# GoldenCheetah: Core / View Split

## Technical Design Document

**Status:** Draft for review, do not implement yet
**Date:** October 30, 2026
**Version:** 0.1
**Audience:** Implementation team, maintainers, reviewers
**Goal:** Define how to separate GoldenCheetah into a headless core plus train/control engine below, and interchangeable views above, so the same lower layers can power the current Qt GUI, a QML GUI, a web dashboard, and a game engine.

---

## 1. Purpose

GoldenCheetah is a large, long-lived Qt desktop application for cyclists, runners, triathletes, and coaches. It covers planning, analysis, modeling, cloud sync, and indoor training with ANT+ and BTLE trainers.

Today those capabilities live in one tightly integrated Qt application. That makes it hard to:

- Build a second GUI without dragging in the first GUI
- Run training headless for tests, automation, or a server
- Drive a game engine from the same workout and device logic
- Test device, workout, and recording logic without a window
- Reuse analysis and modeling code in other tools

This document proposes a fundamental split: data plus control layers below, view/GUI layers above. It is a design proposal, not code. No implementation should start until the open decisions in Section 15 are resolved.

---

## 2. Goals

### 2.1 Primary goals

1. **One core, many views.** The same core and train engine power the current Qt Widgets GUI, a future QML GUI, a web dashboard, and a game engine.
2. **Headless train engine.** Workout execution, device control, and recording run with no window, no event loop dependency, and no chart code.
3. **Clean device seam.** ANT+, BLE, virtual, and file-replay devices are interchangeable behind one interface.
4. **Deterministic, testable control.** Workout logic is a pure state machine driven by an injectable clock. Device behavior can be simulated with a fake device.
5. **Incremental migration.** The existing GUI keeps working at every phase. No big-bang rewrite.
6. **Same truth everywhere.** Recording, live metrics, and rendering all see the same telemetry sample. The view never computes workout state.

### 2.2 Secondary goals

7. **Separate chart data from chart rendering.** Core returns plain series. Qwt, QML, or web charts render them.
8. **Make analysis reusable.** Metrics, models, zones, and data filters operate on core types, not on widgets.
9. **Preserve user data and workflows.** Athlete directories, workout library, and file formats do not change in the first phases.

## 3. Non-goals

This design does not propose:

- A rewrite of GoldenCheetah in another language
- A new file format for rides or workouts
- A new cloud service or new device protocol
- Removal of the current Qt Widgets GUI
- A full microservice architecture
- A change to GPL-2.0 licensing, see Section 13 for linking implications
- A final decision on build system, see Section 15

If a proposal requires any of the above, it is out of scope for this document.

---

## 4. Current State

### 4.1 What exists today

The repository has a flat `src/` directory plus `test/` and `unittests/`, with a single build. The important domain centers are `Athlete`, `Context`, and `RideFile` / `RideItem`, with `Context` holding information about the current athlete, main window, and tab.

Train supports ANT+ and FE-C trainers plus BTLE sensors for power, heart rate, and speed/cadence. Train also covers device discovery, pairing, workout control, live charts, and recording.

### 4.2 Where coupling lives

1. **God context.** `Context` is passed widely and can reach sideways into GUI, settings, and the selected tab. Core logic can depend on view state.
2. **Train view owns too much.** Device list management, pairing logic, start/stop/pause semantics, workout execution, lap handling, calibration, recording, and calculations live close to widgets.
3. **Shared mutable realtime state.** Realtime data is shared and mutable. It is easy for a chart to show one value and the file to record another.
4. **Chart preparation tied to rendering.** Analysis often prepares data directly for Qwt, instead of returning plain series that any renderer could use.
5. **Timing tied to UI.** Control and updates are interleaved with Qt timers, signals/slots, and widget updates.

None of this means the domain model is wrong. It means the domain model needs a boundary that prevents it from depending on the view that displays it.

---

## 5. Design Principles

1. **Dependencies point down only.** Presentation depends on train and core. Train depends on core. Core depends on nothing above it.
2. **Engine owns time, devices, workout, and recording.** Views observe and send intents. Views never drive the loop.
3. **Immutable telemetry.** Once a sample is emitted, it is not mutated. Views can cache, plot, log, or ignore it.
4. **Intents, not pokes.** Views send `startWorkout`, `pause`, `setMode`, `setSimGradient`. They do not call hardware directly.
5. **Explicit dependencies.** No globals for settings, athlete, or current selection in core. Pass `AthleteProfile`, clock, recorder, and device list explicitly.
6. **Qt Core is allowed in core, Qt Widgets and Qwt are not.** `QString`, `QDateTime`, and `QSettings` are not the problem. A `RideFile` that needs a `QWidget*` is the problem. The long-term target is plain C++ plus Qt Core only in core, with a thin Qt adapter in the GUI layer.
7. **Strangler pattern.** Extract leaves first, then the trunk. Keep the existing GUI working at every step.
8. **Prove with a second client.** A design is not done when the old GUI still works. It is done when a tiny second client works without reaching into internals.

---

## 6. Target Architecture

### 6.1 Layer overview

```
Presentation
  Qt Widgets GUI | QML GUI | Web Dashboard | Game Engine
  Observes telemetry, sends intents. No view depends on another view.

Train / Control Engine
  TrainSession | WorkoutPlayer | Recorder | Live Metrics
  Owns session lifecycle, workout execution, trainer commands, recording.

Devices / IO
  IDevice | ANT+ | BLE | Virtual / Fake | File IO | Cloud Sync
  Translates hardware and files into core types.

Core Domain
  RideFile | Athlete | Zones | Metrics | Models | Workout | DataFilter
  Pure domain logic. No Widgets, no Qwt, no MainWindow, no views.
```

### 6.2 Layer responsibilities

| Layer | Owns | Must not depend on | Examples |
| --- | --- | --- | --- |
| Core domain | RideFile, RideItem, RideCache, Athlete data, Zones, Measures, Metrics, Models, Workout parsing, DataFilter | Qt Widgets, Qwt, MainWindow, views | Critical Power, W'bal, Banister / PMC, BikeStress, TRIMP |
| Train / control | Session lifecycle, WorkoutPlayer, Trainer control, Recorder, Live derived metrics, Telemetry merging | Any widget, any chart, Qt event loop as timing source | ERG / Slope / Resistance / Sim, Lap, Pause, Recording |
| Devices / IO | Device abstraction, ANT+, BLE, virtual devices, File import/export, Cloud sync, Python / R bridges | Views, except through core types | FIT, TCX, ERG, MRC, ZWO, Strava, TrainerDay |
| Presentation | Layout, charts, device wizard UI, workout list UI, buttons, shortcuts, video pane, Qt adapter | Each other. Every view talks only to core plus train engine | Current Qt GUI, QML dashboard, Game HUD |

### 6.3 What stays in the current TrainView after the split

Stays: layout, Qwt plots, device wizard UI, workout list UI, buttons, keyboard shortcuts, video pane, Qt adapter that translates observer callbacks into widget updates.

Leaves: device list management, pairing logic, start/stop/pause semantics, workout execution, lap handling, calibration logic, recording, and any calculation that is not purely about pixels.

---

## 7. Core Contracts

This section defines the seam. These are draft interfaces for review. Names and signatures may change during implementation, but the responsibilities must not.

### 7.1 TelemetrySample

One merged sample. Sensors, virtual power, and simulated feeds all produce the same struct.

```cpp
struct TelemetrySample {
    int64_t monotonic_ms = 0;  // steady clock, for control. Never goes backward.
    int64_t wallclock_ms = 0;  // wall clock, for recording to file.
    double watts = 0;
    double watts_3s = 0;       // computed by TrainSession, not by devices
    double heartrate = 0;
    double cadence = 0;
    double speed_kph = 0;
    double distance_m = 0;
    double altitude_m = 0;
    double gradient_pct = 0;   // what the trainer is currently simulating
    double target_watts = 0;   // what the workout asked for
    bool power_valid = false;
    bool hr_valid = false;
    bool trainer_connected = false;
    int trainer_load_pct = 0;
};

struct DeviceInfo {
    std::string id;            // stable id for config, never a pointer
    std::string name;
    std::string transport;     // "ant", "ble", "virtual", "file"
    bool is_controllable_trainer = false;
    bool has_power = false;
    bool has_hr = false;
    bool has_speed_cadence = false;
};

enum class SessionState {
    Idle, Ready, Running, Paused, PausedWithError, Finished, Error
};
```

Rules:

- Samples are immutable once emitted.
- `monotonic_ms` is for control. `wallclock_ms` is for recording.
- `power_valid` and `hr_valid` must be explicit. A dropout is not zero watts, it is invalid data.
- Devices never set `watts_3s`. That is a session-level derived metric.

### 7.2 Workout and TrainerCommand

```cpp
struct WorkoutStep {
    double duration_secs = 0;
    double target_watts = 0;        // absolute, takes precedence if > 0
    double target_pct_ftp = 0;      // used if target_watts == 0
    double target_gradient_pct = 0; // used if mode is Slope
    std::string label;
};

struct Workout {
    std::string name;
    std::string source_path; // .erg, .mrc, .zwo, etc.
    std::vector<WorkoutStep> steps;
    double total_duration_secs() const;
};

struct TrainerCommand {
    enum class Mode { Idle, Erg, Slope, Resistance, Sim } mode = Mode::Idle;
    double target_watts = 0;
    double gradient_pct = 0;
    int resistance_0_100 = 0;
};
```

Workout parsing belongs in core. `ErgFile` parsing is pure file in, `Workout` out. It needs no hardware and no GUI.

### 7.3 Clock

Time is injectable. Production uses a system clock. Tests use a fake clock.

```cpp
class IClock {
public:
    virtual ~IClock() = default;
    virtual int64_t nowMonotonicMs() const = 0;
    virtual int64_t nowWallclockMs() const = 0;
};

class SystemClock : public IClock { /* steady_clock + system_clock */ };

class FakeClock : public IClock {
public:
    int64_t nowMonotonicMs() const override;
    int64_t nowWallclockMs() const override;
    void advanceMs(int64_t delta_ms);
    void setMonotonicMs(int64_t v);
};
```

No core or train class may call a global clock or a UI timer for control timing. It must receive an `IClock*`.

### 7.4 AthleteProfile

Settings the engine needs, passed explicitly.

```cpp
struct AthleteProfile {
    double ftp_watts = 250;
    double weight_kg = 75;
    double bike_weight_kg = 8;
    double height_cm = 175;
};
```

This replaces hidden reads of global settings inside engine code. The GUI layer is responsible for building this struct from stored settings.

### 7.5 IDevice and ITrainer

One interface for all transports. Sensors and trainers are distinct.

```cpp
class ISampleSink {
public:
    virtual ~ISampleSink() = default;
    virtual void onSample(const std::string& device_id,
                          const TelemetrySample& sample) = 0;
};

class IDevice {
public:
    virtual ~IDevice() = default;
    virtual DeviceInfo info() const = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setSampleSink(ISampleSink* sink) = 0;
};

// Only trainers get this. A power meter is not a trainer.
class ITrainer : public IDevice {
public:
    virtual void setErgMode(double target_watts) = 0;
    virtual void setSlopeMode(double gradient_pct, double weight_kg) = 0;
    virtual void setResistanceMode(int level_0_100) = 0;
    virtual void setSimMode(double gradient_pct, double speed_kph) = 0;
};
```

Design decisions:

- No Qt types in the signature. Use `std::string`, `double`, `bool`. A Qt adapter translates later.
- No widget callbacks. Devices emit samples through `onSample`. The engine owns fan-out.
- Discovery is separate from connection. `DeviceManager::scan()` returns `DeviceInfo` list. `DeviceManager::open(id)` returns an `IDevice*`.
- Pairing config is a list of device ids plus roles: power source, HR source, controllable trainer. That config must be serializable to JSON, not hidden inside device code.
- A smart trainer is both a sensor and a controllable device. It should expose both roles, not conflate them.

### 7.6 WorkoutPlayer

Pure state machine. No threads, no devices, no GUI. Time comes in as a parameter.

```cpp
enum class WorkoutState { Idle, Running, Paused, Finished };

class WorkoutPlayer {
public:
    void load(const Workout& workout, double ftp_watts);
    void start(int64_t now_monotonic_ms);
    void pause(int64_t now_monotonic_ms);
    void resume(int64_t now_monotonic_ms);
    void skipStep(int64_t now_monotonic_ms);
    void seek(double time_secs, int64_t now_monotonic_ms);

    // Called by TrainSession every tick. Returns what the trainer should do now.
    TrainerCommand tick(int64_t now_monotonic_ms);

    WorkoutState state() const;
    bool hasWorkout() const;
    int currentStepIndex(int64_t now_monotonic_ms) const;
    double timeInStepSecs(int64_t now_monotonic_ms) const;
    double timeRemainingSecs(int64_t now_monotonic_ms) const;
    double currentTargetWatts(int64_t now_monotonic_ms) const;
    double elapsedSecs(int64_t now_monotonic_ms) const;
};
```

Rules:

- All getters that depend on time take `now_monotonic_ms`. A getter with no time parameter cannot be correct for a time-driven player.
- Interval logic lives here and only here. The view asks for `timeInStepSecs()` and `currentTargetWatts()`. It never computes them.
- `%FTP` resolution happens here, using the FTP passed to `load()`.

### 7.7 TrainSession

The orchestrator. The only class that knows about devices, player, recorder, and observers together.

```cpp
struct ITelemetryObserver {
    virtual ~ITelemetryObserver() = default;
    virtual void onTelemetry(const TelemetrySample& sample) = 0;
    virtual void onSessionStateChanged(SessionState state) = 0;
    virtual void onWorkoutStepChanged(int index) = 0;
};

class TrainSession : public ISampleSink {
public:
    void addDevice(std::unique_ptr<IDevice> device);
    void setClock(IClock* clock);
    void setRecorder(IRecorder* recorder);
    void setAthleteProfile(AthleteProfile profile);

    // Intents from any view or game. These never touch widgets.
    void loadWorkout(const Workout& workout);
    void start();
    void pause();
    void resume();
    void stopAndSave();
    void lap();
    void setManualCommand(TrainerCommand cmd);
    void setSimGradient(double gradient_pct);

    // Observation
    void subscribe(ITelemetryObserver* observer);
    void unsubscribe(ITelemetryObserver* observer);
    TelemetrySample latest() const;
    SessionState state() const;

    // The loop. Called by a worker thread, e.g. every 250 ms.
    void tick();

    // ISampleSink, called by devices
    void onSample(const std::string& device_id,
                  const TelemetrySample& sample) override;
};
```

`tick()` does the same work every time, in order:

1. Merge latest samples per device into one `TelemetrySample`. If power comes from one device and HR from another, both survive.
2. Decide command: if manual mode, use manual command. Else if workout is running, use `WorkoutPlayer::tick(now)`. Else if sim gradient is active, use Sim mode.
3. Send command to the trainer, if one is connected.
4. Compute live derived metrics in core: 3s power, normalized power, W'bal, TSS so far.
5. Append to `Recorder` at 1 Hz.
6. Publish the sample to observers. Detect step change and fire `onWorkoutStepChanged()`.

That order matters. Recording, live metrics, and rendering all see the same sample.

For observers, use a plain interface in core. If Qt signals are wanted, add a thin Qt adapter in the GUI layer that implements `ITelemetryObserver` and re-emits Qt signals. Core stays free of Widgets, and the GUI stays idiomatic Qt.

### 7.8 Recorder

Recorder is a sink. File writer, in-memory test recorder, and null recorder all implement the same interface.

```cpp
class IRecorder {
public:
    virtual ~IRecorder() = default;
    virtual void start(int64_t wallclock_ms) = 0;
    virtual void onSample(const TelemetrySample& sample) = 0;
    virtual void lap(int64_t monotonic_ms) = 0;
    virtual void finish() = 0;
};
```

The FIT writer is an `IRecorder` implementation in the IO layer. It must not depend on views.

### 7.9 FakeDevice

A fake smart trainer plus power meter, with lag, noise, and dropouts. Manual `tick()` keeps tests deterministic.

```cpp
struct FakeDeviceConfig {
    std::string id = "fake-trainer-1";
    std::string name = "Fake Trainer";
    double initial_watts = 0;
    double noise_watts_stddev = 4.0;
    double base_cadence = 88;
    double base_hr = 125;
    double tau_secs = 3.0;        // how fast watts converge to target
    double hr_tau_secs = 25.0;    // HR is much slower than power
    int64_t dropout_every_ms = 0; // 0 = never drop out
    int64_t dropout_duration_ms = 2000;
};

class FakeDevice : public ITrainer {
public:
    explicit FakeDevice(FakeDeviceConfig config = {});
    // IDevice + ITrainer as in Section 7.5
    // Test harness calls this. In a real driver this would be
    // the ANT / BLE callback thread.
    void tick(int64_t now_monotonic_ms, int64_t now_wallclock_ms);
    double currentWatts() const;
    double currentTargetWatts() const;
};
```

Behavior:

- First-order lag toward target: `watts += (target - watts) * (1 - exp(-dt / tau))`, plus Gaussian noise.
- HR chases a steady-state value derived from watts, with a much slower time constant.
- Cadence collapses when watts fall below threshold.
- Toy speed model: speed derived from watts and gradient, clamped, integrated into distance.
- Dropout window: when in dropout, `power_valid` and `hr_valid` are false. A dropout is not zero watts, it is invalid data.
- `FakeDevice` must not use a Qt timer. The test harness calls `tick()` explicitly, so time is deterministic.

---

## 8. Control Loop, Threading, and Timing

### 8.1 Threads

| Thread | Owns | Must not do |
| --- | --- | --- |
| Device threads | ANT+ / BLE callbacks, USB, parsing | UI updates, file IO on hot path, workout logic |
| Session worker thread | `TrainSession::tick()`, merging, commands, live metrics, recording trigger | Direct widget updates, blocking device IO |
| UI thread | Layout, charts, buttons, Qt adapter | Device IO, workout logic, file writing on hot path |

Devices push samples via `onSample()` on their own threads. `TrainSession` stores latest per device under a mutex. The session worker calls `tick()` on a fixed cadence and publishes to observers. The Qt adapter receives observer callbacks and forwards to the UI thread via queued connections.

### 8.2 Timing targets

- Device sample rate: ANT+ up to 4 Hz, BLE typically 1 Hz, depending on sensor.
- Control tick: 4 Hz, every 250 ms. This is the trainer command cadence.
- Recording: 1 Hz. `TrainSession` downsamples, it does not record every tick.
- UI publish: every tick, but views may throttle rendering to 10 to 30 Hz.
- Live metrics: computed in session tick, not in view paint.

Control must not depend on UI timers or the Qt event loop. If the UI freezes, the workout must keep running and recording.

### 8.3 Merging

Multiple devices produce partial samples. Example: power meter gives watts and cadence, HR strap gives HR, smart trainer gives speed and gradient. `TrainSession` keeps `latest_by_device_id` and merges on tick:

- Take the most recent valid watts, HR, cadence, speed, distance, gradient.
- If a source has not updated for N seconds, mark its fields invalid, do not freeze the session.
- If the trainer disconnects, keep recording sensor data, transition to `PausedWithError`, and let any view offer resume or save.

### 8.4 Error handling

- **Dropout, 1 to 3 seconds:** `power_valid` is false, recorder writes a gap, trainer holds last command. Do not freeze UI thread waiting for a sensor.
- **Trainer disconnect mid-workout:** Transition to `PausedWithError`, keep recorded file safe, let any view offer resume or save. View does not own recovery logic.
- **Workout finished:** `WorkoutPlayer` returns Idle command, session transitions to `Finished`, recorder finalizes file.
- **Manual override:** `setManualCommand()` switches control source to Manual until a workout is loaded or resumed. Workout and manual must not fight.

---

## 9. Data Flow

### 9.1 Sensors to file

```
ANT+ / BLE device -> IDevice -> TelemetrySample
  -> TrainSession::onSample() -> latest_by_device
  -> TrainSession::tick() -> merge -> WorkoutPlayer -> TrainerCommand
  -> ITrainer -> Recorder (1 Hz) -> RideFile / FIT
  -> Observers -> GUI / Game / Web
```

### 9.2 Game to trainer

```
Game: terrain gradient at avatar
  -> TrainSession::setSimGradient(gradient)
  -> TrainerCommand Sim mode
  -> ITrainer::setSimMode(gradient, speed)
  -> TelemetrySample with new gradient and watts
  -> Game: physics from watts, weight, gradient -> avatar speed and position
```

### 9.3 Workout inside a game world

The game does not control resistance. `WorkoutPlayer` controls ERG. The game only renders watts, speed, and avatar position. Both modes work because the engine exposes `setSimGradient()` and `setManualCommand()` as intents, not as Qt slots.

---

## 10. Persistence and Configuration

- Athlete directories, workout library, and file formats do not change in early phases.
- Device pairing config becomes explicit data: list of device ids plus roles. It must be serializable to JSON for tests and for non-Qt clients.
- Core must not read `QSettings` directly. The GUI layer builds `AthleteProfile` and device config and passes them in.
- `TrainSession` must not know about file paths for athlete storage, except for the explicit output path passed to `stopAndSave()` or to the recorder.
- Workout library remains a directory of ERG / MRC / ZWO files. Parsing stays in core.

---

## 11. Analysis, Metrics, and Models

Analysis is the easy win. Inputs and outputs are already fairly pure: a `RideFile` in, metrics and series out.

Rules:

- Metrics are pure functions: `RideFile` plus `AthleteProfile` and zones, returns metric values. No widgets.
- Models (Critical Power, W'bal, Banister / PMC, Virtual Elevation) live in core and operate on core types.
- Chart preparation returns plain series: time, watts, W'bal, PMC, CP curve. Qwt, QML, or web charts render those series.
- Live metrics (3s power, normalized power, W'bal live, TSS so far) are computed in `TrainSession`, not in view paint. Views display numbers.
- DataFilter and user metrics operate on core types. Python and R bridges call core APIs, not GUI objects.

This makes the same analysis available to the GUI, to headless batch jobs, and to a web service, without duplicating logic.

---

## 12. Game Engine Integration

### 12.1 In-process library

For Godot and the Qt GUI, link core plus train engine in-process as a C++ library, or via a stable C ABI wrapper for engines that need it.

Game frame, at 60 fps:

```
session->setSimGradient(terrain_gradient_at_avatar);
sample = session->latest();
avatar_speed = physics_from_power(sample.watts, avatar_weight, gradient);
avatar_position += avatar_speed * dt;
if (session->workoutStepChanged()) {
    hud->show_interval(session->currentTargetWatts());
}
```

The game pulls the latest sample each frame. It does not block on Qt. The engine runs its own worker thread.

### 12.2 Out-of-process daemon

For proprietary engines, add a daemon mode later: `gc-train-daemon` owns `TrainSession` and speaks WebSocket, gRPC, or ZeroMQ. The game is a separate process.

This keeps GPL-2.0 containment clean. In-process linking into a proprietary game is uncomfortable under GPL-2.0. A daemon with a protocol boundary is the safe path for that case. See Section 13.

### 12.3 What the game needs from the engine

- Latest telemetry at frame rate, with explicit validity flags
- Current workout step, target, time in step, time remaining
- Ability to send gradient, and to switch to manual ERG / slope
- Session state, error state, and dropout state
- No Qt, no widgets, no main window

If any game integration needs to reach into GoldenCheetah internals to get one of these, the seam is leaking.

---

## 13. Licensing Note

GoldenCheetah is GPL-2.0. This design does not change licensing, but it affects how the engine can be consumed:

- In-process linking: suitable for GPL-compatible clients, including the Qt GUI and Godot.
- Out-of-process daemon: recommended for proprietary game engines or closed clients. The daemon is GPL-2.0, the client communicates over a protocol boundary.

The implementation team must confirm the daemon protocol and the C ABI wrapper approach with maintainers before promising proprietary integrations. Do not treat in-process linking into a proprietary engine as approved.

---

## 14. Migration Plan

Do not move `TrainView` first. Move leaves first, then the trunk. Keep the existing GUI working at every phase.

### Phase 0: Pure types

Move: `TelemetrySample`, `DeviceInfo`, `Workout`, `WorkoutStep`, `TrainerCommand`, `SessionState`, `AthleteProfile` into `core/`.

Acceptance: Existing GUI compiles against them. No behavior change. Build check added that fails if a GUI header creeps into core.

### Phase 1: Workout parsing plus WorkoutPlayer

Move: `ErgFile` parser plus `WorkoutPlayer` into `core/` and `train/`.

Why safe: No hardware, no threads, fully deterministic.

Acceptance: Unit test loads ERG, ticks fake clock, asserts targets at each second. Existing GUI still uses same workout logic via adapter.

### Phase 2: RealtimeData to TelemetrySample plus TelemetryBus

Move: Shared mutable realtime data to immutable `TelemetrySample` plus a bus / observer fan-out.

Acceptance: Old plots still render, now from bus. No view mutates engine state. Dropout handling is explicit.

### Phase 3: IDevice plus DeviceManager plus FakeDevice

Move: Drivers become implementations of `IDevice` / `ITrainer`. Add `DeviceManager` and `FakeDevice`.

Acceptance: Headless run with `FakeDevice` produces same FIT as before for a scripted workout. Real ANT+ and BLE devices still work in GUI.

### Phase 4: TrainSession plus Recorder

Move: Orchestration, recording, live metrics into `TrainSession` and `IRecorder`.

Acceptance: CLI harness can run a full workout with no window. See Section 14.1.

### Phase 5: Thin TrainView

Move: `TrainView` becomes layout plus Qwt plus buttons plus observer adapter.

Acceptance: User cannot tell from behavior, except bugs go down. A second tiny client works. See Section 14.2.

### 14.1 Headless proof: CLI harness

If this cannot run, the split is not real:

```
gc-train-headless \
  --workout workouts/3x10_at_250.erg \
  --device fake --ftp 280 --weight 75 \
  --duration 1800 \
  --out /tmp/test.fit
```

That command must exercise `WorkoutPlayer`, `TrainSession`, `FakeDevice`, `Recorder`, and FIT writing, with no window, no event loop, and no Qwt.

### 14.2 Second client proof

Do not start with a full second GUI. Build a minimal QML dashboard or a small Godot plugin that subscribes to telemetry and sends commands. If that 200-line client works, the architecture is real. If it needs to reach into internals, the seam is still leaking.

---

## 15. Testing Strategy

### 15.1 Unit tests

- `WorkoutPlayer`: load ERG, tick fake clock, assert targets, pause/resume, skip, seek, finish. No hardware.
- `FakeDevice`: lag, noise, dropout, ERG convergence, slope response.
- `Telemetry` merging: power from one device, HR from another, stale source handling.
- Metrics and models: known `RideFile` in, known metric values out.
- Workout parsing: ERG, MRC, ZWO fixtures.

### 15.2 Integration tests

- `TrainSession` plus `FakeDevice` plus `FakeClock` plus `InMemoryRecorder`: full workout, assert recorded samples at 1 Hz, assert interval averages near targets.
- Dropout test: dropout every 30 seconds for 2 seconds, assert `power_valid` false during dropout, assert recorder writes gap, assert session does not crash.
- Disconnect test: trainer disconnect mid-workout, assert `PausedWithError`, assert file is safe, assert resume works.
- Manual override test: workout running, then manual ERG command, assert trainer receives manual command, not workout command.

### 15.3 GUI and regression tests

- Existing GUI tests keep passing at each phase.
- Headless CLI output compared against GUI-recorded FIT for same scripted workout, within tolerance for noise.
- Performance: control tick must stay within budget under load, no UI thread blocking.

### 15.4 Test harness notes

- Keep a raw pointer to `FakeDevice` before moving it into session, or add a test-only `tickDevices()` to `TrainSession`. Do not add a Qt timer to `FakeDevice` just to make tests pass. Manual ticking is the feature.
- Use `FakeClock` for all time-dependent tests. No `sleep()` in unit tests.

---

## 16. Risks and Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Qt-isms leak into core | Core becomes unusable for games and headless | Build check forbidding Widgets / Qwt includes in core. Code review gate. |
| Global Context persists | Hidden dependencies, hard to test | Split Context into GUI AppContext and core SessionContext. Pass explicitly. |
| Big-bang rewrite temptation | Long branch, merge hell, regressions | Strangler pattern, Phase 0 to 5, GUI works at each step. |
| Driver quirks | ANT+ / BLE edge cases break in abstraction | Keep drivers as implementations, not engine. Add FakeDevice plus real-device soak tests. |
| Threading bugs | Race, freeze, lost samples | One mutex for latest-by-device, one worker thread for tick, no UI work on session thread. |
| GPL linking for games | Legal risk for proprietary clients | Daemon mode for proprietary engines. Confirm with maintainers. |
| Maintainer bandwidth | Partial refactor stalls | Phase 1 is a weekend-sized win. Land it first to prove pattern. |
| User scripts depend on internals | Python / R breakage | Keep core API stable, add adapter, document migration for scripts. |

---

## 17. Alternatives Considered

**A. Keep monolith, add game mode inside Qt GUI.**
Rejected. A game engine still needs to embed Qt and the main window. It does not solve headless testing or second GUI.

**B. Split into many micro-libraries at once.**
Rejected. Splitting into 15 libraries before seams are stable creates build pain without benefit. Start with one `core`, one `train`, one `io`, one `gui-qt`.

**C. Use signals/slots everywhere in core.**
Rejected as sole mechanism. Qt signals are fine in the GUI adapter, but core should use plain interfaces and callbacks so non-Qt clients can consume it without Qt.

**D. Out-of-process daemon only, no in-process library.**
Rejected as first step. Daemon adds protocol and deployment complexity. Start in-process for Qt GUI and Godot, add daemon when a proprietary client needs it.

**E. Rewrite TrainView in QML first.**
Rejected. Rewriting the view before extracting the engine just moves coupling to a new view. Extract engine first, then views become cheap.

---

## 18. Open Questions and Decisions Needed

The implementation team must resolve these before Phase 2:

1. **Qt Core in core:** Is Qt Core allowed in `core/`, or must core be pure std C++? Recommendation: allow Qt Core initially, forbid Widgets / Qwt, revisit pure std later.
2. **Build system:** Stay on qmake with subdirs, or move to CMake? Recommendation: keep current build in Phase 0 to 1, decide before Phase 3.
3. **C ABI wrapper:** Do we ship a stable C API (`gc_session_create`, `gc_session_poll_telemetry`, etc.) for game engines, or C++ only? Recommendation: C ABI for daemon and game clients, C++ for internal use.
4. **Daemon protocol:** WebSocket, gRPC, or ZeroMQ for out-of-process mode? Recommendation: decide in Phase 4, not now.
5. **Qwt replacement:** Does QML GUI use Qt Charts, custom rendering, or keep Qwt for Qt Widgets GUI only? Recommendation: keep Qwt for current GUI, decide separately for QML.
6. **DeviceManager ownership:** Who owns device lifetime, session or manager? Recommendation: session owns devices passed in, manager owns discovery.
7. **Recording format:** Does `IRecorder` write FIT directly, or write `RideFile` then convert? Recommendation: write `RideFile` in core, convert to FIT in IO layer.
8. **Live metrics scope:** Which live metrics are in core in Phase 4? Recommendation: 3s power, normalized power, W'bal, TSS so far. Others later.
9. **Video sync:** How does video playback sync to session time without coupling? Recommendation: video is a view that observes `TelemetrySample.monotonic_ms` and session state.
10. **Calibration:** Where does trainer calibration live? Recommendation: calibration is a device operation, exposed via `IDevice` extension, not in view.

---

## 19. Work Breakdown and Milestones

**Milestone 1: Types and WorkoutPlayer**
Phase 0 plus Phase 1. Deliverables: core types, `WorkoutPlayer`, unit tests, GUI still working.

**Milestone 2: Telemetry and Devices**
Phase 2 plus Phase 3. Deliverables: `TelemetrySample`, `IDevice`, `FakeDevice`, `DeviceManager`, merging, dropout handling.

**Milestone 3: Session and Headless**
Phase 4. Deliverables: `TrainSession`, `IRecorder`, headless CLI, integration tests.

**Milestone 4: Thin View plus Second Client**
Phase 5. Deliverables: thin `TrainView`, Qt adapter, minimal QML or Godot client, docs.

**Milestone 5: Daemon and Game Hardening**
Optional. Deliverables: daemon mode, C ABI, game integration guide, GPL review.

---

## 20. Definition of Done

The split is done when all of the following are true:

1. `core/` builds with no Qt Widgets and no Qwt includes, enforced by build check.
2. `train/` builds with no widgets and no chart code.
3. `gc-train-headless` runs a full workout with `FakeDevice` and writes a valid FIT file, with no window.
4. A second tiny client, QML or Godot, subscribes to telemetry and sends commands without reaching into internals.
5. Existing Qt GUI passes regression tests, with same behavior for real ANT+ and BLE devices.
6. WorkoutPlayer, FakeDevice, and TrainSession have deterministic unit and integration tests using `FakeClock`.
7. Dropout and disconnect handling are tested and documented.
8. Device config is serializable and does not depend on GUI settings code in core.

---

## Appendix A: File Layout for Review

```
core/
  telemetry.h       TelemetrySample, DeviceInfo, SessionState
  workout.h         Workout, WorkoutStep, TrainerCommand
  clock.h           IClock, SystemClock, FakeClock
  athlete_profile.h AthleteProfile
devices/
  idevice.h         ISampleSink, IDevice, ITrainer
  fake_device.h     FakeDevice, FakeDeviceConfig
  fake_device.cpp   Lag, noise, dropout, toy speed model
train/
  workout_player.h  WorkoutPlayer state machine
  workout_player.cpp Tick, pause, resume, skip, seek
  recorder.h        IRecorder, NullRecorder, InMemoryRecorder
  train_session.h   TrainSession, ITelemetryObserver
gui-qt/
  TrainView.cpp     Layout, Qwt, buttons, Qt adapter only
  TrainAdapter.cpp  ITelemetryObserver to Qt signals
apps/
  gc-train-headless.cpp CLI harness
```

## Appendix B: FakeDevice Behavior for Tests

- Power converges to target with first-order lag, time constant `tau_secs`.
- Noise is Gaussian, seeded for determinism.
- HR chases `base_hr + watts * 0.18` with slower time constant.
- Cadence collapses when watts fall below threshold.
- Speed is a toy model from watts and gradient, integrated into distance.
- Dropout sets `power_valid` and `hr_valid` to false for `dropout_duration_ms`, every `dropout_every_ms`.
- No Qt timer. Test harness calls `tick(now_monotonic_ms, now_wallclock_ms)` explicitly.

---

## Appendix C: Example Headless Test Flow

```
FakeClock clock;
FakeDevice device(config);
TrainSession session;
session.setClock(&clock);
session.setRecorder(&recorder);
session.setAthleteProfile({ftp 280, weight 75});
session.addDevice(device);
session.loadWorkout(workout);
session.start();
Loop: clock.advanceMs(250); device.tick(clock.now...); session.tick();
Assert: recorder has 1 Hz samples, interval averages near targets.
```

This flow must work before Phase 5 is considered done.

---

**End of document.** For review, focus on Section 7 contracts, Section 8 threading, Section 14 migration, and Section 18 open questions. Implementation should not start until those are agreed.
