# Zwift-like game on the Godot plugin — support plan

Draft, 2026-11-18 (revised same day: the physics already
exists — see "The physics already exists" below). Not built
yet. This plan covers what the core + plugin need so a
separate Godot app can be a simple Zwift-like game. The app
itself is not built here.

## The game, scoped

- No multiplayer, no gamification (XP, levels, unlocks), no
  equipment customization.
- One rider. Profile screen: name, height, weight, FTP (and
  bike weight, default 8 kg). These map to GoldenCheetah's
  built-in profile — with one split, below.
- Two ride modes, toggled in the ride UI:
  - Free ride: the rider's watts come from their legs (or the
    virtual input, below), resistance follows the course
    gradient. No ERG.
  - Fixed ERG: a set wattage, adjustable up and down 5 W at a
    time from the UI. Gradient is ignored while ERG is on.
- Minimal controls beyond that: pause/resume, stop, and the
  ride HUD (watts, 3 s power, speed, distance, gradient,
  NP / W'bal / TSS).

## What already works today

Checked against the code (2026-11-18), not assumed. The
surprising part is how much of the game the session already
does — the gaps are concentrated in one place, the device.

- **Free ride without a workout works.** TrainSession's header
  pins it: "A session with no workout can still run (free
  ride, manual or sim commands); loadWorkout is optional, not
  required." `start()` with no workout just skips the player;
  the session runs, records, and computes metrics.
- **The command priority is the game's mode switch.**
  TrainSession decides each tick: manual override first, else
  workout, else sim gradient, else Idle. So the app sets the
  course gradient as the sim gradient every frame, and the
  ERG button sets a manual ERG command — which overrides the
  course automatically. Clearing the manual command returns
  to the course gradient. No mode logic needed in the app
  beyond "ERG on / ERG off".
- **Fixed ERG ± 5 W already works.** `set_manual_erg` is in
  the C ABI and bound on the node. The app keeps the current
  ERG target (or reads `target_watts` from telemetry while
  ERG is active) and calls `set_manual_erg(target ± 5)`,
  clamped at 0. The example scene already does the manual
  ERG flow.
- **Live metrics are free.** NP, W'bal, TSS so far are
  computed by the session every tick and exposed through the
  C ABI and the node. The HUD reads them.
- **The profile struct already has height.** `AthleteProfile`
  is ftp_watts, weight_kg, bike_weight_kg (8), height_cm
  (175). The C ABI's `gc_session_create` only passes FTP and
  weight — height and bike weight are stuck at defaults.
  The fix is at the C ABI boundary, not in core.
- **The profile split (name).** The name does not belong in
  core: `AthleteProfile` is documented as "the settings the
  train engine needs", and the engine never uses a name. In
  GoldenCheetah the rider name is a GUI/athlete concept, not
  an engine input. So: name lives in the app (profile screen,
  HUD, saved profile file) and never crosses into core.
  Height, weight, FTP, bike weight cross at session creation.
- **Telemetry already carries the fields the game needs.**
  `gc_telemetry_t` has speed_kph, distance_m, altitude_m,
  gradient_pct, target_watts. The node just doesn't expose
  distance / altitude / gradient yet (it caches only watts,
  3 s, HR, cadence, speed, target, step, state, times,
  metrics, validity, elapsed). Exposing them is a trivial
  plugin change — but see the distance gap below, because
  today nothing populates distance_m.

## The physics already exists — surface it, don't rebuild it

The first draft of this plan said to build a speed model in
core. That was wrong: GoldenCheetah already has one, and the
GUI already uses it to compute virtual speed.

- `src/Train/PhysicsUtility.h/.cpp`:
  `computeInstantSpeed(weightKG, slope, altitude, watts,
  crr = 0.004, Cm = 1.0, Cd = 1.0, A = 0.5, T = 293.15)` —
  speed in km/h from the power balance (gravity + rolling +
  aero, kreuzotter.de equations, ISA air pressure/density),
  solved closed-form with a cubic solver. Pure standard C++:
  `<cmath>` only. Its one dependency, `BlinnSolver.h` in
  `src/Metrics/`, is also Qt-free and already in the build.
- Production use, `src/Train/TrainSidebar.cpp` (~line 2169):
  every telemetry update the train view computes
  `weightKG = bicycle.MassKG()` (rider + bike) and
  `computeInstantSpeed(weightKG, slope, altitude, watts)`
  with the defaults, and stores it as the virtual speed.
  That is the same calculation the game needs, already
  validated by the GUI's own rides.
- Hardware-less precedent: `NullController` (the no-device
  controller) already rides with no hardware, computing
  speed from principles instead of randomising it.
- The fuller model, `src/Train/BicycleSim.h/.cpp`, adds
  momentum: wheels with rotational inertia, kinetic energy,
  watt-spike filtering, an altitude penalty. It is the nicer
  model, but it is coupled to the GUI — it takes a `Context*`
  and reads `RealtimeData` / `ErgFile` — so it cannot move
  into core as it stands. It is a later step if the game
  wants acceleration smoothing (port the stateful part to
  core taking watts/slope/altitude/dt and keeping velocity,
  dropping the Context dependency). The instant function is
  enough to start: it is stateless, already proven, and the
  game can add momentum later without changing the seam.

Surfacing it is a move, not a build: move
`PhysicsUtility.h/.cpp` into `src/Core/`, update the paths
in `src/src.pro`, add the files to
`util/check-core-includes.sh`, and add a unit suite pinning
`computeInstantSpeed` at known inputs (flat / climb /
descent / 0 W) so the move is verified behaviour-neutral.
The GUI keeps calling the same function; only its file
location changes. The virtual device then calls it each
tick with current watts, slope, and altitude.

## The gaps

### 1. The FakeDevice cannot power a game (core — the big one)

FakeDevice is a test double: in slope/sim mode it reports a
fixed 150 W, speed is `30 − gradient` kph in slope mode and
0 in sim mode (the session passes speed 0 to `setSimMode`),
weight is recorded but never used for physics, and generated
samples carry no distance or altitude. It was built to make
session tests deterministic, not to be ridden.

What the game needs instead is a **virtual rider device**
(`ITrainer`, new core type, not an extension of FakeDevice —
the fake stays a test double):

- The app drives the rider's watts into it; it reports speed
  from the surfaced `computeInstantSpeed` (weight = rider +
  bike from `AthleteProfile`, slope and altitude from the
  current tick), accumulates distance and altitude from its
  own speed each tick, and synthesises plausible HR/cadence
  (fixed defaults to start, like the fake's 120 bpm /
  90 rpm, or simple functions of watts later).
- In ERG mode it converges to the target like the fake does
  (tau ~1000 ms), so the existing proofs keep their numbers.
- A real trainer reports cumulative distance the same way,
  so the device — not the app — is the right place for
  distance: the session merges it, the recorder gets it,
  and the app reads it.
- **A watts input for playing without hardware.** With no
  trainer, watts have to come from somewhere. The app
  drives them: keyboard/buttons raising and lowering effort
  (e.g. hold a key to push harder, or +/− buttons), mapped
  to a watts value the app hands to the device each frame.
  That is the game's "legs" until a real trainer exists.
  The input model itself lives in the app; core only takes
  the watts number.

### 2. C ABI: profile, device choice, virtual watts (C ABI + plugin)

The C ABI is the seam, and it needs three additions. All are
additive — the existing `gc_session_create` stays as it is,
so the C harness, the layer test, and the main proof keep
passing unchanged.

- **A profile struct at creation.** `gc_session_create` takes
  only FTP and weight. Add `gc_rider_profile_t` (ftp_watts,
  weight_kg, height_cm, bike_weight_kg) and a new create
  function taking it plus the device kind (below), e.g.
  `gc_session_create_rider`. The plugin gains `height_cm`
  and `bike_weight_kg` properties (defaults 175 / 8) next to
  the existing `ftp_watts` / `weight_kg`, read by `_ready`
  the same way.
- **Device choice at creation.** The C ABI hardcodes one
  FakeDevice. The game needs the virtual rider device
  instead. The new create function takes which device to
  build; the old create keeps building the fake. (Device
  scan/select for real hardware is the later gated step —
  see "Later, gated".)
- **Virtual watts + the missing telemetry fields.** Bind a
  `gc_session_set_virtual_watts` (and the node's
  `set_virtual_watts`) so the app can drive the rider's
  watts each frame, and expose `get_distance_m`,
  `get_altitude_m`, `get_gradient_pct` on the node — the
  telemetry already carries them, the node just doesn't
  cache them.

### 3. Sim gradient at 0% kills the session (core — small fix)

`TrainSession::setSimGradient` sets `sim_active_ =
gradient_pct != 0`. A flat course section is 0%, so the sim
turns off, the command falls through to Idle, and the
device generates nothing — telemetry stalls and the merge
goes stale after 3 s (power_valid false). The game would
freeze on every flat. Fix: track "sim has been set"
separately from the value, so 0% is a live gradient, not
"no sim". Small change, covered by a session test. Do it
before the game depends on the sim path.

### 4. The course lives in the app, not in core

Core never needs to know about courses. The app owns:

- Course data: a first course as a simple list of segments
  (start_m, end_m, gradient_pct) in a JSON/Resource, or a
  gradient function of distance. Loop it or make it
  out-and-back — either is fine to start.
- Gradient lookup: each frame, read `distance_m` from the
  node, look up the gradient, call `set_sim_gradient`.
- The visual world: road pitch matching the course gradient
  (so climbs look like climbs), rider avatar position from
  distance along the course, chase camera.
- Profile screen + persistence: name, height, weight, FTP
  in a Godot `ConfigFile` or JSON under `user://`; loaded at
  startup, applied to the session node's properties before
  `_ready` (scene-file values or set before the node enters
  the tree — same mechanism the example scene uses).
- Ride flow: menu → profile → ride scene (node with
  `auto_load_workout` off, no workout loaded) → `start()`
  → `_process` ticks with the real frame delta (the
  `fixed_step_ms` proof cadence stays a proof tool) →
  pause / stop. ERG toggle and ± 5 W buttons in the HUD.
- Recording: not in the first playable version. When wanted,
  wire a `CsvRecorder` into the C ABI session (the recorder
  exists in core; the C ABI currently uses `NullRecorder`).
  FIT stays the separate gated step it already is.

## Sequence

Follow the established pattern: plan first, then small
verified steps, stopping on anything major. Order matters —
each step is usable and testable on its own.

1. **Sim-gradient 0% fix** (core, small). Session test:
   sim 0% keeps the trainer commanded and samples flowing.
2. **Surface the physics** (move, not a build). Move
   `PhysicsUtility.h/.cpp` into `src/Core/`, update
   `src/src.pro`, add to `check-core-includes.sh`, add a
   unit suite pinning `computeInstantSpeed` at known inputs
   (flat / climb / descent / 0 W). Exit: the GUI build
   still passes with the same `--version`, the suite passes,
   and the numbers match what the GUI computed before the
   move.
3. **Virtual rider device** (core + C ABI + plugin). New
   `ITrainer` using the surfaced function, ERG convergence
   like the fake, speed from physics, distance/altitude
   accumulated in the device, watts input via the new C ABI
   setter. Exit: headless proof through the node — free ride
   at a set virtual watts on a flat and on a gradient,
   distance increasing, speed responding to gradient — plus
   the existing test, example, and main proof all still
   passing (the old create path still builds the fake).
4. **Profile through the C ABI** (C ABI + plugin). Profile
   struct, new create function, `height_cm` /
   `bike_weight_kg` node properties. Exit: example scene
   (or a variant) configured with a non-default height
   shows the physics responding — same watts, different
   height/weight, different speed.
5. **Game app** (separate repo, plugin as a submodule under
   `addons/gctrain/`, per the standalone-app write-up):
   profile screen, course data + gradient lookup, ride
   scene + HUD, ERG toggle and ± 5 W controls, pause/stop,
   simple 3D world. Exit: headless run of the ride scene
   completing a short free ride and an ERG stint, with
   distance and speed sane throughout.
6. **Recording** (C ABI + app, optional for the first
   playable version): CSV recorder wired in, ride saved on
   stop.

## Later, gated — not part of this plan

- **Real trainer.** Headless part DONE 2026-11-18 (see the
  scope doc `doc/core-view-split/real-trainer-ftms-scope.md`
  and Status below): `Bt40DeviceAdapter` (src/Train,
  ITrainer + ICalibration) over the existing BT40 stack,
  queue-and-drain on the session tick, stable
  `ble:<address>` ids, FTMS protocol suite (19 tests),
  adapter suite (14 tests, stubbed backend) and scanner
  suite (7 tests). The transport decision from this bullet
  is settled: FTMS over BLE via the BT40 stack, adapter
  not native rewrite. What is NOT done and stays gated:
  hardware validation (discovery/pairing on a real trainer,
  ERG convergence, spin-down, soak, regression vs stock
  GoldenCheetah on the Zwift Hub — needs a
  Bluetooth-capable host), C ABI scan/select, and the
  TrainSidebar switch. The game code doesn't change: the
  app talks to the session, not to the device.
- **BicycleSim momentum model in core.** DONE 2026-11-18
  (authorised "Both, BicycleSim first"): the fuller model
  (wheels, kinetic energy, watt filtering) is ported to
  `src/Core/BicycleSim.h/.cpp` + `src/Core/Integrator.h`,
  hypoxia out for v1, full 28-value config, both dt
  overloads, initial velocity 0, pinned by a 15-test suite.
  VirtualRider is wired to it, so speed now carries
  momentum through gradient and watts changes instead of
  jumping instantly. See Status below for the measured
  numbers.
- **FIT recording.** Gated already (Phase 4 scope). CSV
  first.
- **Workouts in the game.** The player, parser, and adapter
  already exist — a workout mode is a later app feature,
  not a core gap.
- **Everything out of scope above** (multiplayer,
  gamification, equipment): each is its own plan when
  wanted. None of them changes the core pieces above;
  they build on the session the same way.

## Decisions (accepted 2026-11-18)

- Name stays app-side, never enters core — the engine has no
  use for it, and `AthleteProfile` is documented as engine
  settings only.
- New create function carrying the profile struct and the
  device kind, old create kept for the proofs — keeps the
  stable C ABI stable.
- Distance and altitude accumulate in the virtual device,
  like a real trainer reports them, not in the app — one
  source of truth, and the recorder gets it.
- Recording waits until after the game is playable, CSV
  first.

Support work (steps 1–4) authorised 2026-11-18, in small
verified steps with at least one commit per step; the app
(step 5) is a separate piece of work and is not authorised
by accepting these decisions.

## Status, 2026-11-18

- Step 1 DONE (b4807f48d): `setSimGradient` always sets
  `sim_active_`, so 0% is a live flat, not "no sim"; new
  test `simGradientZeroIsLiveNotOff` (trainSession suite
  now 17 Totals), verified failing on the old code.
- Step 2 DONE (d4fb7bf8e, plus 8197f32b4 untracking the
  build artifacts the commit accidentally included):
  PhysicsUtility moved `src/Train/` → `src/Core/`, new
  physicsUtility suite pins the pre-move values (flat
  250 W 32.08 kph, climb 8% 12.48 kph, descent −5%
  48.49 kph, 0 W 0). GUI build re-run and clean (qmake 0,
  sub-src 0, --version V3.8 (5015)), check-core-includes
  PASS on 20 files.
- Step 3 DONE (55d6f2097, f8ee33ca6, this commit):
  VirtualRider (core, header-only ITrainer) + virtualRider
  suite (9 tests, 11 Totals); C ABI `gc_rider_profile_t`,
  `gc_device_kind_t`, `gc_session_create_rider`, and
  `gc_session_set_virtual_watts` (additive — old create
  kept, C harness still PASS with identical numbers);
  plugin `device_kind` property, `set_virtual_watts`,
  distance/altitude/gradient getters; freeride proof scene
  PASS (40 s, 200 W flat then 8%: flat 29.6 kph, climb
  10.2 kph, distance 220 m). `godot-test.sh` now runs the
  layer test, the example, and the freeride proof.
  Build fix: SConstruct links PhysicsUtility.cpp +
  BlinnSolver.cpp (see the Godot plan doc).
- Step 4 DONE (this commit): `height_cm` (175) /
  `bike_weight_kg` (8) node properties, read by `_ready`
  into the C ABI profile (no more literals). Proof is
  `godot/freeride-profile.tscn`, reusing freeride.gd with
  tighter bounds: a 100 kg rider, 190 cm, 10 kg bike at
  200 W does 29.1 kph flat and 7.8 kph at 8% over 204 m —
  against 29.6 / 10.2 / 220 m for the 75 kg rider — so the
  climb is where the weight shows. The script also fails
  fast if the node properties do not round-trip the scene
  values. Height is carried in the profile but has no
  speed effect in the instant model (Cd is fixed); a
  height-dependent CdA is later work. `godot-test.sh` now
  runs four checks (layer test, example, freeride,
  freeride-profile), all PASS.
- BicycleSim port + wiring DONE (2026-11-18, commits
  6ac763b46 scope doc, 174d4791a + c4ba5a8fd probe,
  4ec90a1dc port, 00e65a4c6 pinning suite, 26fc0db8d
  wiring): `git mv` of BicycleSim.h/.cpp + Integrator.h
  into src/Core, `BicycleConfig` (28 values, SimBicyclePage
  defaults), `configure()`, both `SampleSpeed` overloads,
  watt filter in, hypoxia out (field kept, default 1.0,
  inert). GUI call sites updated with no behaviour change
  (TrainSidebar, NullController, plus the third caller the
  scoping missed, `SimBicyclePage::SetStatsLabelArray`).
  Probe (GUI model, before the port): KE mass 82.545 kg at
  75 kg rider, wheels +0.5325 kg each; steady states 250 W
  flat 32.12 kph, 150 W flat 26.52 kph, 250 W at 8% 12.69
  kph; spike filter clamps to prev + 200 W. Pinning suite
  (15 tests, dt-explicit, no sleeps) reproduces the probe
  to ±0.5% steady state / ±2% distance. Wiring: VirtualRider
  drives the sim with {watts, gradient, altitude} and the
  device dt; distance from the sim's d, altitude still in
  the device; `start()` clears the bike. The virtualRider
  suite's speed pins are now steady-state checks (drive
  120 s, same 0.05 kph tolerance). Freeride proofs
  RE-MEASURED with momentum (deterministic, fixed 250 ms
  steps): 75 kg rider at 200 W — flat 26.5 kph at 20 s
  (steady 29.55), climb 10.2 kph at 40 s (steady 10.16),
  distance 184 m; 100 kg rider — flat 24.3 kph, climb
  7.8 kph, distance 159 m. The old instant-model numbers
  (29.6 / 10.2 / 220 m and 29.1 / 7.8 / 204 m) are no
  longer the target; the bounds were tightened so the
  instant model would fail. Build fix: BicycleSim.cpp
  needed the `#include <cmath>` it only got transitively
  in the GUI build — the standalone SCons compile failed
  without it; SConstruct links BicycleSim.cpp the same way
  as PhysicsUtility.cpp.
- Real trainer, headless part DONE 2026-11-18 (commits
  cf3377484 scope doc, 9fbf629dd FTMS helpers extracted,
  854874a10 FTMS protocol suite, 7a5a9a1a0 adapter +
  scanner, 43fff8564 ICalibration + CalibrationData move,
  b4d0ebcce adapter + scanner suites): `Bt40DeviceAdapter`
  (src/Train, ITrainer + ICalibration) translates the
  session's commands into the legacy BT40 calls and polls
  the legacy telemetry snapshot into TelemetrySamples via
  tick() — queue-and-drain, no core change, no mutex.
  Backend seam (IBt40Backend) with a production
  implementation wrapping BT40Controller + one BT40Device
  created directly for the scanned target, and a blocking
  scanner mapping trainers to ble:<address> ids. Minimal
  additive legacy changes only (link-state flag +
  isConnected, public setLoadIntensity, controller
  forwarders); the legacy DeviceInfo class was renamed
  Bt40DeviceInfo — it collided with core DeviceInfo in any
  TU including both headers. FTMS suite: 19 tests (flags
  parsing, feature/target parsing, capping, exact
  control-point bytes). Adapter suite: 14 tests (16
  Totals); scanner suite: 7 tests (9 Totals). Hardware
  part NOT done: discovery/pairing, ERG convergence,
  spin-down, soak, and the regression vs stock
  GoldenCheetah all need a Bluetooth-capable host + the
  Zwift Hub. C ABI scan/select and the TrainSidebar switch
  are still gated.
