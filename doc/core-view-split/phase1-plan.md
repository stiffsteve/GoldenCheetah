# Phase 1 plan: Workout parsing plus WorkoutPlayer

Status: in progress, 2026-10-31. TDD section 14, Phase 1, and section
7.6 (WorkoutPlayer). Phase 0 is complete on branch
`core-view-split-phase0` (see `phase0-inventory.md`).

## What Phase 1 is, in this codebase

TDD Phase 1 says: move `ErgFile` parser plus `WorkoutPlayer` into
core/train, with a unit test that loads an ERG, ticks a fake clock,
and asserts targets at each second, while the existing GUI still
works. Three scope decisions make that concrete here, and they are
recorded so they can be challenged:

1. **TrainSession, FakeDevice, and Recorder are NOT Phase 1.**
   The TDD describes TrainSession in section 7.7 and its proof in
   14.1, but its own phase list puts TrainSession/Recorder in
   Phase 4 and FakeDevice in Phase 3. Building them in Phase 1
   would silently expand Phase 1 into Phases 3–4. Phase 1 builds
   only the parser and the player, which are the parts that are
   deterministic, hardware-free, and fully testable alone.

2. **The core parser covers ERG and MRC only, not all formats.**
   Reimplementing every legacy format (CRS, ERG2, ZWO, TTS, PGMF,
   code) in pure C++ is not a Phase 1 step; it is a separate
   project. ERG (absolute watts) and MRC (percent of FTP) are the
   time-based Computrainer formats that map cleanly onto
   `WorkoutStep`. CRS is distance-based (DISTANCE GRADE WIND), not
   time-based, so it does not fit `WorkoutStep` at all and is
   explicitly out of scope: the parser returns an error for it,
   rather than silently producing wrong durations. ZWO and the
   others are likewise errors in Phase 1, not silent guesses.

3. **The GUI is NOT switched to WorkoutPlayer in Phase 1.**
   The TDD's "GUI still uses same workout logic via adapter" is
   satisfied here by providing the adapter, not by switching the
   GUI. TrainSidebar's workout logic uses `ErgFileQueryAdapter`
   (wattsAt/gradientAt, laps, texts, slope/CRS routes, and more)
   that WorkoutPlayer does not yet cover. Switching it is a
   behaviour-risk change that needs its own gated step, with
   trainer-session testing, after Phase 1. Phase 1 therefore
   delivers the player, the parser, and the adapter, with the GUI
   untouched except for build-file lists. If that switch is wanted
   in Phase 1, it is a separate decision, not an omission.

A fourth, technical decision: **the core parser preserves file
semantics and defers resolution to the player.** The legacy
`ErgFile` parser bakes the athlete's CP into points at parse time
(MRC percent becomes absolute watts, ERG watts are scaled by
CP/FTP). The core parser does not: MRC stays as `target_pct_ftp`,
ERG stays as absolute `target_watts`, and `WorkoutPlayer::load()`
resolves percent using the FTP passed to it. That is the TDD's
rule (section 7.6: "%FTP resolution happens here, using the FTP
passed to load()"), and it means the core parser and the legacy
parser will disagree on absolute watts for MRC files by design.
The Train adapter (step 3) goes through the legacy parser, so it
inherits the legacy conversion and produces absolute-watt steps;
that difference is documented in the adapter header.

## Steps (each a small commit, with verification)

1. **WorkoutPlayer** (`src/Core/WorkoutPlayer.h`, header-only,
   pure std C++): TDD 7.6 API exactly — load/start/pause/resume/
   skipStep/seek/tick, plus state/hasWorkout/currentStepIndex/
   timeInStepSecs/timeRemainingSecs/currentTargetWatts/elapsedSecs,
   all time-dependent getters taking `now_monotonic_ms`. Rules
   (documented in the header): elapsed time freezes while paused;
   tick returns Idle when Idle/Paused/Finished; step targets
   resolve watts > percent > gradient; seeking/skipping clamp to
   [0, total]; an empty workout finishes on start; time beyond
   total reports Finished. No threads, no devices, no GUI, no
   global clock — time is a parameter, so `FakeClock` drives it
   in tests by passing `nowMonotonicMs()`.

2. **ERG/MRC parser** (`src/Core/WorkoutParser.h`, header-only,
   pure std C++): `parseWorkout(content, source_path)` returning
   a result with `ok`, a `Workout`, and an `error` string on
   failure. Parses `[COURSE HEADER]` (DESCRIPTION, FILE NAME,
   MINUTES WATTS vs MINUTES PERCENT), `[COURSE DATA]` points
   (minutes to seconds, absolute watts or percent, including
   `50%` suffix lines), and converts points to steps the way
   `ErgFile::Sections()` does (a point's value applies until the
   next point; a first point after time 0 creates an initial
   step). Consecutive steps with identical targets are merged
   (total duration unchanged). Errors: no course data, no points,
   malformed data line, CRS/ZWO/other unsupported format.

3. **Train adapter** (`src/Train/WorkoutAdapter.h`, header-only,
   in Train not Core): `workoutFromErgFile()` converts an already-
   parsed legacy `ErgFile` to a `Workout` via `ErgFile::Sections()`
   and the Phase 0 `workoutStepFromSection()` mapping. Only
   ERG/MRC (hasWatts) files produce steps; CRS/slope files return
   an empty workout (not time-based), documented. This adapter is
   NOT checked by `util/check-core-includes.sh` (boundary code).
   It cannot be unit-tested without linking `ErgFile` (which
   drags in Context/GUI, see Phase 0 step 4), and it is not
   compiled into the GUI binary yet (no production file includes
   it; qmake HEADERS lists do not compile headers), so it is
   verified in Phase 1 by a standalone syntax check only, with
   that limitation stated in the header.

4. **Unit tests** (`unittests/Core/workoutPlayer/`, new QtTest
   suite): player tests with `FakeClock` (no `sleep()` anywhere)
   covering load/start/tick targets per second, step boundaries,
   %FTP resolution and watts precedence, slope steps, pause/
   resume time freezing, skip, seek (including clamping and
   finish), empty workout, and finish; parser tests covering ERG
   absolute watts, MRC percent, merged steps, header name, and
   error cases (CRS, empty, malformed). Assertions are the exit
   proof; the GUI build and all baseline suites must still pass.

5. **Exit check**: `util/check-core-includes.sh` passes on the
   Phase 0 files plus the two new core headers; GUI builds and
   `--version` matches the Phase 0 exit (V3.8, Qt 6.8.3, 382
   metrics, DB schema 160); all suites pass (baseline 8 + mapping
   + new player suite); no GUI behaviour change (no TrainSidebar
   or other GUI file changed). Phase 1 is done when all of these
   hold, not when the code merely compiles.

## What Phase 1 deliberately does not do

- No TrainSession, no IDevice/FakeDevice, no Recorder, no live
  metrics (3s power, NP, W'bal, TSS) — Phases 3–4.
- No CRS/ZWO/TTS/PGMF/ERG2/code parsing in core.
- No GUI switch to WorkoutPlayer, no change to TrainSidebar,
  no change to workout display, laps, or text cues.
- No change to the qmake build system or directory layout
  (Q2 decision, before Phase 3).
