# Godot second-client plan

Status: COMPLETE, 2026-11-17. Branch `core-view-split-phase0`,
after Phase 5 (`93da91cac`). TDD section 14.2 (second client
proof), section 12 (game engine integration), section 13
(licensing), Q3 (C ABI wrapper, accepted 2026-10-30).

## Exit check, verified 2026-11-17

- C ABI harness (`src/Core/gc-session-c-test.c`, plain C):
  PASS, final NP=218.8 Wbal=20000 TSS=1.37, watts55=250.0 —
  identical to the Phase 5 console dashboard run.
- Godot headless run (`godot --headless --path godot`, 2.3 s
  real time for 65 simulated seconds): exit 0, step 0 then
  step 1, manual ERG 100 W sent at 30 s and cleared at 40 s,
  final NP=218.6 Wbal=20000 TSS=1.37, "godot-client: PASS".
  NP differs from the dashboard by 0.2 (one-frame lag: the
  parent scene reads the session before the child ticks it
  that frame); W'bal and TSS match exactly.
- Versions: Godot 4.5.1-stable (editor binary, ~/godot/),
  godot-cpp 4.5-stable (e83fd09), SCons 4.11.1. Godot 4.7.2
  was the latest release, but godot-cpp had no 4.7 tag, so
  4.5.1 was chosen as the latest version with a matching
  godot-cpp. Install: Godot binary + godot-cpp clone +
  `pip install --user --break-system-packages scons` (plain
  pip install fails on Ubuntu 24.04, PEP 668).
- Build fixes, recorded so they are not re-discovered:
  `register_types.h` must include `<godot_cpp/godot.hpp>`,
  not `<godot_cpp/core/defs.hpp>` — `ModuleInitializationLevel`
  lives in godot.hpp. `main.gd` must not annotate the session
  variable with the `GcTrainSession` type, and must type
  `elapsed` explicitly (`var elapsed: float = ...`); with the
  extension unloaded the type does not exist and the script
  fails to parse. The project must be imported once
  (`godot --headless --editor --quit --path .`) before a
  plain run, or the extension is not in `.godot/
  extension_list.cfg` and the class is a placeholder. (The
  first import crashed with signal 11 while main.gd still
  had the parse error; after fixing the script it exits 0.)
- Godot layer test (`godot/test_gctrain_session.gd`, added
  2026-11-17, run with `./scripts/godot-test.sh`): PASS, 23
  checks, exit 0, twice in a row. A SceneTree script, no
  window/GPU/framework: one 250 ms fixed step per frame
  through the GDExtension node, asserting what the HUD and
  a game read — state/step at start, target and validity
  flags after the first device tick, manual 100 W target at
  35 s and 250 W after clearing at 45 s, watts > 200 W at
  55 s, step 1 / 150 W at 60 s with the step_changed signal,
  pause freezing time-in-step with resume back to Running,
  NP 200–235 / W'bal 19000–20000 / TSS 1.0–1.7 at 65 s, and
  Finished after stop. Bounds mirror the C harness. Two
  timing facts the test records: telemetry fields only exist
  after the first tick (target after tick 1, validity flags
  after the device has ticked once), and the node's cached
  state is only refreshed by its next `_process`, so the
  Finished check waits one frame after stop.
- Example scene (`godot/example.tscn` + `example.gd` +
  `workouts/demo.erg`, added 2026-11-18, also run by
  `./scripts/godot-test.sh`): PASS, exit 0. Shows the flow
  a real app uses instead of the proof defaults: the node is
  configured in the scene file (auto_load_workout off,
  ftp_watts 300, weight_kg 80), the script loads the workout
  from disk with FileAccess and starts explicitly, then runs
  the same 65 s proof and checks its own result. Final
  NP=218.6 Wbal=20000 TSS=0.95 — TSS differs from the main
  run (1.37) because FTP is 300 here, which is the proof the
  scene's ftp_watts reached the session. The test above is
  deliberately untouched by this work: it keeps the proof
  defaults and pins the auto-load behaviour.
- Node API added for the example (plugin change only, no
  core change — the C ABI already had `gc_session_create`
  taking FTP/weight and `gc_session_load_workout_text`):
  `ftp_watts` / `weight_kg` / `auto_load_workout`
  properties (defaults 250 / 75 / true, read by `_ready`),
  plus bound `load_workout_text(text, source_path)` and
  `start_session()` returning the C ABI status. Load fails
  with GC_ERR_STATE unless the session is Idle, which is why
  auto_load_workout must be off to load from a scene. One
  build fix recorded: godot-cpp's `String::utf8()` returns
  a `CharString`, not a `PackedByteArray`.
- `util/check-core-includes.sh`: PASS on 19 files (new
  GcSessionC.h added).
- All suites pass: 13 test dirs, 0 failed (fakeDevice 18,
  phase0Mapping 16, season 32, seasonOffset 8, seasonParser
  3, signalSafety 4+3+3, splineCrash 3, telemetryBus 17,
  trainSession 16, units 8, utils 3, workoutPlayer 17,
  trainSessionAdapter 7). Note: a VM refresh during this
  work wiped /usr dev deps (libEGL.so.1 missing, so the
  test binaries would not load) and the apt cache was
  empty (system replay autoclean); the baseline script's
  dpkg-cache fallback then killed the script under
  `set -e` before the apt install could run. Fixed in the
  script (fallback is now `|| true`) and deps restored via
  `--deps-only`, exit 0.
- No TrainSidebar, TrainBottom, plot, or controller file
  changed, so no GUI behaviour change. No GUI build was
  re-run: no `src/` GUI file changed and GcSessionC.h is
  not referenced by the qmake build; the Godot work lives
  in `godot/` and `src/Core/` only.

Decision recorded 2026-11-17: the user chose the Godot plugin
over the QML dashboard as the second-client form. Phase 5
itself was already complete as scoped on 2026-11-01 (adapter
plus form-neutral console dashboard proof); this work is the
graphical second client that Phase 5 deliberately did not
build. It does not re-approve Phase 5, and it does not start
the TrainSidebar switch, FIT conversion, or the pilot plot
switch, which stay gated.

## What this is, and what it is not

TDD 14.2: build a minimal QML dashboard or a small Godot
plugin that subscribes to telemetry and sends commands. If
that ~200-line client works without reaching into internals,
the architecture is real.

TDD 12.1: for Godot, link core plus train engine in-process
as a C++ library, or via a stable C ABI wrapper for engines
that need it. TDD 12.2 / 19: the out-of-process daemon is
later, for proprietary engines; it is explicitly rejected as
a first step. TDD 13: in-process linking is suitable for
GPL-compatible clients, including Godot; the daemon is the
safe path only for a proprietary/closed client. So this work
is in-process, no daemon, and the daemon protocol (Q4)
stays deferred.

What the game needs from the engine (TDD 12.3): latest
telemetry at frame rate with explicit validity flags,
current workout step / target / time in step / time
remaining, ability to send gradient and switch to manual
ERG / slope, session state, error state, and dropout state.
No Qt, no widgets, no main window.

## Inventory (step 0 findings)

- No C ABI wrapper exists yet. Q3 accepted a stable C API
  (`gc_session_create`, `gc_session_poll_telemetry`, etc.)
  for daemon and game clients, C++ internally, but Phases
  0-5 built no such wrapper. The Godot plugin cannot talk
  to `TrainSession` (C++, header-only, `std::unique_ptr`
  devices, `ISessionObserver`) without one. The wrapper is
  therefore step 1, before any Godot code.
- No Godot installed on this VM (`which godot` empty,
  2026-11-17). Godot 4.x plus godot-cpp must be installed
  as part of step 2; the exact stable version is recorded
  when installed, and the install is reproducible from the
  steps in this plan, not from a one-off binary left in
  /tmp.
- `TrainSession` (Phase 4, `src/Core/TrainSession.h`):
  header-only pure std C++, owns devices passed in,
  caller-owned clock/recorder/observers, no threads, no
  tickDevices(). Tests keep a raw `FakeDevice*` and tick
  it themselves. The C wrapper must therefore own the
  FakeClock and the FakeDevice internally and drive both
  from one `gc_session_tick(dt_ms)` call, so a game client
  never has to tick a device itself.
- `FakeDevice` (Phase 3): deterministic, manual tick,
  ERG first-order convergence, slope model, dropout
  intervals. Empty script generates samples by converging
  to the ERG targets the session sends it — exactly what
  the console dashboard and headless harness already use.
- Console dashboard proof (`src/Core/gc-train-dashboard.cpp`,
  Phase 5): 2-step workout (60 s at 250 W, 60 s at 150 W),
  FakeClock advancing 250 ms per loop, manual ERG 100 W
  at 30 s cleared at 40 s, verified 65 s run, final NP
  218.8, W'bal 20000, TSS 1.37. The Godot client repeats
  this same workout and the same manual-command proof, so
  its numbers are comparable against a known-good run.
- `WorkoutParser` (Phase 1): `parseWorkout(text, path)`
  returns ERG absolute watts / MRC percent only; CRS and
  other formats are errors. The C wrapper loads a workout
  from ERG/MRC text through this parser, not by
  re-implementing parsing.

## Scope decisions

1. **C ABI first, Godot second.** The wrapper is usable
   and tested on its own (plain C harness, no Godot), so a
   Godot build failure cannot mask a wrapper bug, and the
   wrapper stays useful for any future non-Qt client.
2. **Opaque handle, plain C structs, error codes.**
   `gc_session_t` is an opaque pointer; telemetry and live
   metrics cross as plain C structs with fixed layout
   (doubles, int64, ints for validity flags). No C++
   exceptions, no C++ types, no strings across the
   boundary. Every function null-checks its handle and
   returns a `gc_status` code; a null handle is an error,
   never a crash.
3. **FakeDevice only, in this work.** The wrapper creates
   one FakeDevice trainer internally. Real-device support
   goes through DeviceManager / IDevice conversion, which
   is gated work (TrainSidebar switch prerequisites), not
   this client. The plugin proves the seam, it does not
   ship hardware support.
4. **Single-threaded, caller-serialised.** Same rule as
   TrainSession: no threads in core, the caller (Godot's
   main thread, via `_process`) calls tick and poll on one
   thread. Godot pulls the latest sample each frame; it
   does not block on Qt, and there is no Qt in this path
   at all.
5. **Minimal scene, ~200 lines of GDScript.** HUD labels
   for watts / 3s watts / HR / cadence / speed, current
   step and target, session state and dropout flags, live
   metrics (NP, W'bal, TSS), plus buttons/keys for manual
   ERG, slope, sim gradient, pause/resume. No art, no
   physics, no avatar: the game loop in TDD 12.1 (avatar
   speed from power) is out of scope; the client proves
   telemetry in and commands out, which is what 14.2 asks
   for.
6. **No GUI change.** No TrainSidebar, TrainBottom, plot,
   or controller file changes. No `src/` GUI build change
   beyond the new core header (which is pure std C++ and
   is added to `util/check-core-includes.sh`).

## Steps (each a small commit, with verification)

1. **C ABI wrapper** (`src/Core/GcSessionC.h`, header-only
   pure std C++ internally, C API at the boundary):
   `gc_session_create(ftp_watts, weight_kg)`,
   `gc_session_destroy`, `gc_session_load_workout_text`,
   `gc_session_start/pause/resume/stop_and_save`,
   `gc_session_tick(dt_ms)` (advances FakeClock, ticks
   FakeDevice, ticks session), `gc_session_poll_telemetry`,
   `gc_session_live_metrics`, `gc_session_state`,
   `gc_session_current_step`, `gc_session_target_watts`,
   `gc_session_set_manual_erg`, `gc_session_clear_manual`,
   `gc_session_set_sim_gradient`, `gc_session_set_slope`.
   Step/time remaining are derived from the player via
   public accessors only.
2. **C harness test** (`src/Core/gc-session-c-test.c`,
   standalone, manual gcc build, not qmake): runs the
   dashboard workout (60 s at 250 W, 60 s at 150 W) through
   the C API only, sends manual ERG 100 W at 30 s and
   clears it at 40 s, asserts step changes, target watts,
   and that converged watts are near target late in each
   step. No Godot, no C++ in the test program.
3. **Godot install + GDExtension plugin**
   (`godot/` at the repo root, outside `src/`): Godot 4.x
   stable plus godot-cpp, built with SCons. A
   `GDExtension` class `GcTrainSession` wrapping the C ABI
   (one session, created on ready, ticked from `_process`
   with real frame delta, polled each frame into exported
   properties/signals). The plugin links the core
   in-process; it does not link Qt.
4. **Minimal Godot scene** (`godot/project.godot`,
   `main.tscn`, `main.gd`, ~200 lines GDScript): the HUD
   and controls in scope decision 5, running the same
   2-step workout with the same manual-command proof, in
   headless mode (`godot --headless --path godot`) so it
   is verifiable without a window or GPU.
5. **Exit check**: C harness passes; Godot headless run
   completes the 65 s proof run and prints step 0 then
   step 1, manual sent then cleared, and final NP/W'bal/
   TSS consistent with the console dashboard run (NP
   ~218.8, W'bal 20000, TSS ~1.37, allowing for the
   frame-rate tick cadence); `check-core-includes.sh`
   passes on the new header; all 14 existing suites pass;
   no TrainSidebar/plot/controller file changed.

## What this deliberately does not do

- No daemon mode (deferred Q4), no WebSocket/gRPC/ZeroMQ.
- No TrainSidebar switch, no pilot plot/bus consumer switch.
- No FIT writer (separate gated IO step; the recorder here
  is NullRecorder, as in the dashboard proof).
- No real-device support, no driver conversion.
- No avatar physics, no game art, no second workout format
  beyond ERG/MRC text through the existing parser.
- No QML dashboard (user chose Godot).
