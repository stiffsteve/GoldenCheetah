# Phase 4 plan: TrainSession plus Recorder

Status: in progress, 2026-11-01. TDD section 14, Phase 4,
and sections 7.7 (TrainSession), 7.8 (Recorder), 8
(threading, in part), and 15.2 (integration tests). Phase 3
is complete on branch `core-view-split-phase0` (see
`phase3-plan.md`); the player (Phase 1), bus and merge
(Phase 2), and device interfaces, fake, and manager (Phase 3)
are what this phase orchestrates.

## What Phase 4 is, in this codebase

TDD Phase 4 says: orchestration, recording, and live metrics
move into `TrainSession` and `IRecorder`. Acceptance: a CLI
harness can run a full workout with no window (section 14.1).

Four scope decisions make that concrete here, recorded so
they can be challenged, following the Phase 1/2/3 pattern:

1. **No FIT writer in Phase 4 core.** TDD Q7, already
   accepted, says core writes `RideFile` and FIT conversion
   lives in the IO layer. The legacy `RideFile` and
   `FitRideFile` are Qt-bound (`QList`, `QString`, and more)
   and cannot be linked into a pure core test, and a pure
   std C++ FIT writer written without validation against
   legacy FIT output would be worse than no FIT writer: it
   could produce corrupt files that look plausible. Phase 4
   therefore delivers `IRecorder` plus `NullRecorder`,
   `InMemoryRecorder`, and a `CsvRecorder` whose columns
   match the fields a `RideFile` point carries (time, watts,
   3s watts, HR, cadence, speed, distance, altitude,
   gradient, target, validity flags, lap index), and a
   headless harness that writes that CSV. The FIT conversion
   (core samples to a validated FIT file, compared against
   legacy GUI-recorded FIT for the same scripted workout,
   per TDD 15.3) is a separate, gated IO step, not claimed
   here. The headless acceptance that IS claimed here is
   the core half: a full workout runs with no window, no
   event loop, and no Qwt, driven by manual ticks and a fake
   clock, and its recorded samples are asserted.

2. **Session observer naming deviates from TDD 7.7.**
   TDD 7.7 names the session observer `ITelemetryObserver`
   with `onTelemetry`, `onSessionStateChanged`, and
   `onWorkoutStepChanged`. Phase 2 already shipped an
   `ITelemetryObserver` with only `onTelemetry` for the
   bus, and changing it now would break Phase 2's tests and
   its bus contract. Phase 4 therefore defines
   `ISessionObserver` with all three methods, and leaves the
   bus observer untouched. A future Qt adapter can implement
   either interface; core does not force the choice.

3. **No threads in core session code.** TDD section 8
   describes devices pushing on their own threads and a
   session worker ticking on a fixed cadence, with a mutex
   around latest-per-device. Phase 4 core is single-threaded
   and manual, like every previous phase: `onSample()` stores
   latest per device, `tick()` merges and commands, and the
   caller drives both. The header documents that a
   multi-threaded caller must serialize `onSample()` and
   `tick()` itself, exactly as `TelemetryBus` already
   documents. Threading belongs to the future GUI/worker
   adapter, not to core, and adding a mutex now would not
   make the core tests more deterministic — it would make
   them less so.

4. **Live metrics are pure functions, not session magic.**
   TDD Q8, already accepted, says Phase 4 live metrics are
   3s power, normalized power, W'bal, and TSS so far, computed
   in `TrainSession`, not in view paint. Phase 4 implements
   them as pure functions in `LiveMetrics.h` (history plus
   athlete profile in, metric values out), which the session
   calls on each tick. That keeps them testable without a
   session, and makes the provisional choices explicit:
   W'bal uses FTP as the critical-power proxy and a 20,000 J
   W' capacity (standard defaults, not athlete-specific
   values, because `AthleteProfile` carries no CP/W' fields);
   normalized power uses the standard Coggan 30-second
   rolling average, fourth power, average, fourth root.

## Inventory (docs-only, step 0)

- `WorkoutPlayer` (Phase 1, `src/Core/WorkoutPlayer.h`):
  load/start/pause/resume/skipStep/seek/tick, with
  `tick(now)` returning a `TrainerCommand` and
  `currentStepIndex(now)` for step-change detection.
- `TelemetryMerge` (Phase 2, `src/Core/TelemetryMerge.h`):
  `mergeSources(sources, now, stale_timeout_ms)` — power
  from newest fresh power-valid source, HR from newest
  fresh HR-valid source, rest from newest sample overall,
  dropout as invalid plus last known, never zero.
- `IDevice`/`ITrainer`/`FakeDevice`/`DeviceManager`
  (Phase 3): session owns devices passed to it (TDD Q6),
  devices emit through `ISampleSink`, trainers receive
  commands through `ITrainer` setters, `FakeDevice` is
  manually ticked and records commands.
- `SessionState` (Phase 0): Idle, Ready, Running, Paused,
  PausedWithError, Finished, Error — the lifecycle
  vocabulary this phase finally drives.
- Legacy recording: `RideFile` (`src/FileIO/RideFile.h`)
  and `FitRideFile` are Qt-bound and are NOT linked into
  core. No legacy recording code changes in Phase 4.

## Steps (each a small commit, with verification)

1. **TrainSession** (`src/Core/TrainSession.h`, header-
   only, pure std C++): implements `ISampleSink`, owns
   devices (`addDevice(unique_ptr<IDevice>)`, session sets
   itself as sink), holds a `WorkoutPlayer`, a clock
   (`IClock*`, default `SystemClock`, tests use `FakeClock`),
   an `IRecorder*` (not owned), an `AthleteProfile`, and
   an `ISessionObserver` list. `tick()` does TDD 7.7's work
   in TDD 7.7's order: (1) merge latest per device via
   `TelemetryMerge` (stale timeout configurable, default
   3000 ms — the Phase 3/4 production value decision),
   (2) decide command (manual override first, else workout
   player while Running, else sim gradient, else Idle),
   (3) send to the first connected `ITrainer`,
   (4) compute live metrics from history,
   (5) record at 1 Hz (not every tick),
   (6) publish to observers and fire step-change events.
   Lifecycle: loadWorkout (Ready), start (connect/start
   devices, player start, recorder start, Running), pause
   (player pause, Paused, no recording and no trainer
   command while paused), resume (player resume, Running),
   lap (recorder lap), stopAndSave (recorder finish,
   devices stop/disconnect, Finished). Disconnect: if the
   trainer was connected and is no longer connected while
   Running, transition to PausedWithError (player paused,
   recorder kept open so the file is safe); resume
   reconnects and resumes. No `tickDevices()` method:
   tests keep a raw `FakeDevice*` before moving it into
   the session, as TDD 15.4 suggests, and tick it
   themselves — manual ticking stays the feature.

2. **LiveMetrics** (`src/Core/LiveMetrics.h`, header-only,
   pure std C++): pure functions over a history of
   (monotonic_ms, watts) samples plus `AthleteProfile`:
   3s power (mean of power-valid watts in the last 3 s),
   normalized power (Coggan 30 s rolling, fourth power),
   W'bal (Skiba differential model, CP = FTP proxy,
   W' = 20,000 J, recharge tau from Skiba's formula),
   TSS so far (duration times NP times IF over FTP, times
   100). All windows are by monotonic time, not by sample
   count, so they are correct at any tick cadence.

3. **Recorder** (`src/Core/Recorder.h`, header-only, pure
   std C++): `IRecorder` exactly as TDD 7.8 (start with
   wallclock, onSample, lap, finish), `NullRecorder`
   (no-op), `InMemoryRecorder` (stores samples, laps,
   start/finish state, for tests), and `CsvRecorder`
   (writes the sample columns listed in decision 1 to a
   file on start, closes on finish, exposes `ok()` for
   open failure — the TDD interface returns void, so
   failure is reported, not thrown).

4. **Headless harness** (`src/Core/gc-train-headless.cpp`,
   standalone, NOT wired into qmake, built manually with
   g++): `gc-train-headless --workout file.erg --ftp 280
   --duration 1800 --out out.csv` runs a full workout with
   a `FakeDevice` trainer (empty script, ERG-generated
   samples), a `FakeClock` advancing 250 ms per loop
   iteration, a `TrainSession`, and a `CsvRecorder`, with
   no window, no event loop, and no Qwt. It prints the
   recorded sample count and per-step average watts. FIT
   output is not claimed (see decision 1).

5. **Unit and integration tests**
   (`unittests/Core/trainSession/`, new QtTest suite):
   session lifecycle (Idle/Ready/Running/Paused/Finished),
   workout command flow (player target reaches trainer,
   asserted via FakeDevice's recorded command),
   manual override (manual ERG beats workout command),
   sim gradient (no workout, sim command sent),
   disconnect mid-workout (PausedWithError, recorder
   still open, resume works),
   dropout (power_valid false during dropout, recorder
   writes the gap sample, no crash),
   recording at 1 Hz (ticks at 250 ms produce one recorded
   sample per second, not four),
   step change events (observer sees each step index once),
   live metrics (constant power gives NP equal to that
   power, TSS after 10 min at FTP near 16.7, W'bal stays
   full below CP and falls above it),
   and the full-workout integration test (FakeDevice plus
   FakeClock plus InMemoryRecorder, full workout, recorded
   samples at 1 Hz, interval averages near targets).
   No `sleep()`; all time is FakeClock.

6. **Exit check**: `util/check-core-includes.sh` passes on
   the Phase 3 files plus the new core headers; GUI builds
   and `--version` matches the Phase 3 exit (V3.8, Qt 6.8.3,
   382 metrics, DB schema 160); all suites pass (baseline
   8 + mapping + workoutPlayer + telemetryBus + fakeDevice
   + trainSession); no GUI behaviour change (no GUI,
   driver, or controller file changed; only build-file
   lists gain the new headers). Phase 4 is done when all
   of these hold.

## What Phase 4 deliberately does not do

- No FIT writer and no `RideFile` conversion (separate,
  gated IO step with legacy-output validation).
- No GUI TrainView switch (Phase 5), no second client
  (TDD 14.2, Phase 5), no daemon mode (deferred Q4).
- No legacy `RealtimeController` conversion, no ANT/BLE
  driver work.
- No threads, no Qt timers, no global clock in core.
- No laps/text cues/slope routes in the player beyond
  what Phase 1 already covers; TrainSidebar's lap/text
  behaviour is not replicated here.
