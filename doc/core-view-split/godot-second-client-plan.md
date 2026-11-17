# Godot second-client plan

Status: IN PROGRESS, 2026-11-17. Branch `core-view-split-phase0`,
after Phase 5 (`93da91cac`). TDD section 14.2 (second client
proof), section 12 (game engine integration), section 13
(licensing), Q3 (C ABI wrapper, accepted 2026-10-30).

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
