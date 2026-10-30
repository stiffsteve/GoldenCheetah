# Core / View Split — Phase 0 Inventory and Plan

**Status:** Inventory and planning only. No code has been moved yet.
**Branch:** `core-view-split-phase0`
**Base:** v3.8 tag, commit `409a550d2bca4e553535893aae65494da259fdb0`
  (`Version 3.8 release`), verified by the baseline build on 2026-10-30:
  qmake with home Qt 6.8.3, Qwt built, `src/GoldenCheetah` linked with
  0 errors, all 8 unit-test suites passing.
**Design doc:** The user-supplied TDD, draft v0.1, 2026-10-30
  (Core / View Split). Its Section 18 decisions are all accepted except
  Q4 (daemon protocol) and Q5 (Qwt replacement), which are deferred.
  In particular: Q1 — Qt Core is allowed in core for now, Qt Widgets and
  Qwt are forbidden; Q2 — keep the current qmake build for Phases 0–1,
  decide qmake vs CMake before Phase 3.

This document corrects the TDD where v3.8 differs from it, inventories
where the Phase 0 types actually live today, and lays out Phase 0 as a
sequence of small, separately committable steps. Each step must leave
the existing GUI building and the baseline tests passing.

## 1. Corrections to the TDD's description of v3.8

1. **`src/` is not flat.** It is already subdivided. Header / source
   counts at v3.8:
   - `src/Core`: 30 headers, 29 cpp
   - `src/Train`: 98 headers, 91 cpp
   - `src/Gui`: 67 headers, 67 cpp
   - `src/Charts`: 67 headers, 62 cpp
   - `src/FileIO`: 62 headers, 81 cpp
   - `src/Metrics`: 20 headers, 44 cpp
   - `src/Cloud`: 36 headers, 35 cpp
   - `src/ANT`: 6 headers, 5 cpp
   - plus `src/Planning`, `src/Python`, `src/R`, `src/Resources`
2. **There is no single `TrainView`.** The train UI in v3.8 is
   `src/Train/TrainSidebar` (a `GcWindow`), `src/Train/TrainBottom`
   (a `QWidget`), `src/Train/RealtimePlotWindow`, workout windows, and
   related windows. The TDD's "thin TrainView" (Phase 5) therefore
   means thinning this cluster, not one file.
3. **The TDD's `core/` collides with an existing directory.**
   `src/Core` already exists. Phase 0 must not create a second,
   top-level `core/` alongside it while the qmake build is unchanged
   (Q2). Recommendation: put the new Phase 0 headers under the
   existing `src/Core/` (new files, e.g. `TelemetrySample.h`), and
   revisit the TDD's Appendix A layout (`core/`, `devices/`, `train/`,
   `gui-qt/`, `apps/`) only when the build-system decision (Q2) is made
   before Phase 3.
4. **The Phase 0 types do not exist yet.** Phase 0 is therefore not
   "move these types" — it is "define these types as new, pure
   structs, then migrate call sites onto them one at a time". See §2.

## 2. Phase 0 types: what exists today

### 2.1 TelemetrySample — closest match: `RealtimeData`

- Location: `src/Train/RealtimeData.h` / `.cpp`.
- Shape today: a **mutable** class with ~70 data series
  (`enum dataseries`: `Time`, `Watts`, `Speed`, `Cadence`, `HeartRate`,
  `Load`, `Slope`, `Distance`, `Altitude`, `Wbal`, `XPower`, `BikeScore`,
  averages, lap values, muscle-oxygen, VO2, pedal metrics, …), a setter
  and getter per field (`setWatts` / `getWatts`, …), trainer status
  booleans (`getTrainerReady`, `getTrainerRunning`,
  `getTrainerCalibRequired`, `getTrainerConfigRequired`,
  `getTrainerBrakeFault`), and a `spinScan[24]` array.
- Time today: `long` msecs / lap msecs, plus a
  `std::chrono::high_resolution_clock::time_point` for wheel-RPM sample
  time. There is no monotonic-vs-wallclock split and no injectable
  clock.
- Validity today: **implicit**. A dropout is not represented as
  "invalid data"; fields are just numbers, so zero and "no data" are
  indistinguishable — exactly the TDD §4.2 problem.
- Leaks to strip before any move: `#include <QApplication>` (a Widgets
  header in Qt 6), `#include "ErgFileBase.h"` (workout format), and
  `Q_DECLARE_TR_FUNCTIONS` / translation. The TDD's `TelemetrySample`
  is a plain struct with explicit `power_valid` / `hr_valid` /
  `trainer_connected` flags and separate `monotonic_ms` /
  `wallclock_ms`. A field-mapping table (old series → new fields) is
  required; most of the ~70 series are **not** in the TDD's sample and
  belong in a later, wider telemetry type or in derived-metrics code,
  not in Phase 0.
- Consumers: `Context::notifyTelemetryUpdate(const RealtimeData &)`
  (`src/Core/Context.h`), `RealtimeController::getRealtimeData` /
  `pushRealtimeData` / `processRealtimeData` (`src/Train/`).

### 2.2 DeviceInfo — closest matches: `DeviceType` + `DeviceConfiguration`

- `src/Train/DeviceTypes.h`: `DeviceType` with an integer bitmask type
  (`DEV_ANTPLUS`, `DEV_BT40`, `DEV_CT`, …), a connector kind
  (`DEV_SERIAL`, `DEV_TCP`, `DEV_USB`, `DEV_LIBUSB`, `DEV_BTLE`),
  `name`, `description`, `image`, and `realtime` / `download` flags.
- `src/Train/DeviceConfiguration.h`: `DeviceConfiguration` with
  `type`, `name`, `portSpec`, `deviceProfile` (used by ANT to store
  ANT IDs), `wheelSize`, `inertialMomentKGM2`, `stridelength`,
  `postProcess`, `virtualPowerDefinitionString`, and — a coupling to
  note — a raw `RealtimeController *controller` pointer.
  `DeviceConfigurations` already serialises / deserialises a list
  (`readConfig` / `writeConfig`), but the format is not the TDD's
  JSON list of device ids plus roles.
- Missing for the TDD's `DeviceInfo`: a **stable string id** (never a
  pointer), explicit role flags (`is_controllable_trainer`, `has_power`,
  `has_hr`, `has_speed_cadence`), and a `transport` string
  (`"ant"`, `"ble"`, `"virtual"`, `"file"`). Deriving a stable id from
  type + `portSpec` is an open sub-decision for Phase 0, not covered
  by the TDD's Section 18 list.

### 2.3 Workout / WorkoutStep — closest matches: `ErgFile` + `ErgFileBase`

- `src/Train/ErgFileBase.h`: `enum class ErgFileFormat`
  (`unknown`, `erg`, `mrc`, `crs`, `erg2`, `code`),
  `enum class ErgFileType` (`unknown`, `erg`, `slp`, `code`),
  `enum class ErgFilePowerZone` (z1–z10), and `class ErgFileBase`.
- `src/Train/ErgFile.h`: `ErgFilePoint` (`x` = time in msecs or
  distance in metres, `y` = load in watts or altitude, `val` = value
  to send to the device, plus `lat`/`lon`), `ErgFileSection`
  (duration, start, end), `ErgFileZoneSection`, `ErgFileText`,
  `ErgFileLap`, and `ErgFileQueryAdapter` with `wattsAt(msec, lapnum)`,
  `gradientAt(meters, lapnum)`, `altitudeAt`, `locationAt`.
- There is **no `Workout` / `WorkoutStep` class**. The TDD's step
  (`duration_secs`, `target_watts` or `target_pct_ftp`,
  `target_gradient_pct`, `label`) must be derived from
  `ErgFilePoint` / section data, with `%FTP` resolution deferred to
  Phase 1's `WorkoutPlayer` (which takes FTP at `load()` time).
- Couplings to strip: `ErgFile`'s constructor takes a `Context *`,
  and `ErgFile.h` includes `Context.h`, `Zones.h`, `Settings.h`,
  `<QtGui>`, and `<QTextEdit>` (a Widgets header). Workout parsing
  cannot move to core until the `Context *` dependency and the
  Widgets includes are removed or adaptered. This is the single
  largest Phase 0/1 risk after `Context` itself (see §3).

### 2.4 TrainerCommand — no equivalent struct

- Today, trainer commands are virtual methods on
  `RealtimeController` (`src/Train/RealtimeController.h`):
  `setLoad(double)`, `setGradient(double)`, `setMode(int)`,
  `setWindSpeed`, `setWeight`, `setRollingResistance`,
  `setWindResistance`, `setWheelCircumference`, plus calibration
  getters/setters (`getCalibrationType`, `setCalibrationState`, …).
- Mode today is an `int` plus `ErgFileFormat` / `ErgFileType`, not
  the TDD's `Mode { Idle, Erg, Slope, Resistance, Sim }` with
  `target_watts` / `gradient_pct` / `resistance_0_100`.
- Phase 0 defines the struct only; Phase 1 / Phase 4 wire it into
  `WorkoutPlayer` / `TrainSession`. Do not change controller
  behaviour in Phase 0.

### 2.5 SessionState — no equivalent enum

- Today, session/trainer state is scattered: the `RealtimeData`
  trainer booleans (§2.1), and `RealtimeController::start` /
  `restart` / `pause` / `stop` returning `int`.
- The TDD's `SessionState`
  (`Idle`, `Ready`, `Running`, `Paused`, `PausedWithError`, `Finished`,
  `Error`) needs a mapping table defined in Phase 0, including what
  "PausedWithError" means for the existing trainer booleans and
  controller return codes. That mapping is a Phase 0 deliverable, not
  something to improvise during Phase 4.

### 2.6 AthleteProfile — closest matches: `Athlete` + `Zones::ZoneRange`

- `src/Core/Athlete.h`: `Athlete : public QObject` with `cyclist`,
  `zones_` / `hrzones_` maps by sport, PMC / Banister caches, and
  ride-management methods. It is a full application object, not a
  profile struct, and must not move to core as-is.
- `src/Metrics/Zones.h`: `ZoneRange` with `begin` / `end` (`QDate`),
  `cp`, `aet`, `ftp`, `wprime`, `pmax`, and `zonesSetFromCP`.
  **FTP is per zone range, not a single number** — the TDD's
  `AthleteProfile { ftp_watts, weight_kg, bike_weight_kg, height_cm }`
  must therefore be built by the GUI layer from the *current*
  `ZoneRange` plus stored weight/height settings, and that choice
  (which range, which sport) must be explicit in the adapter.
- Settings today live in `src/Core/Settings.h` (`GSettings` /
  `AthleteQSettings`, wrapping `QSettings`). Per the TDD and Q1, core
  must not read `QSettings` directly; the GUI layer builds
  `AthleteProfile` and passes it in.

### 2.7 Clock — no injectable clock

- `RealtimeData` uses `long` msecs; `RealtimeController` uses `QTime`
  for calibration timestamps; device code uses
  `std::chrono::high_resolution_clock` for wheel-RPM timing.
- Phase 0 defines `IClock` / `SystemClock` / `FakeClock` as new,
  header-only (or small) types in `src/Core/`. No existing code is
  rewired to them in Phase 0; Phase 1's `WorkoutPlayer` is the first
  consumer, and its tests are the first proof.

## 3. Boundaries and leakage in `src/Core` today

`src/Core` is in better shape than the TDD's "god context" framing
suggests, with three specific exceptions:

1. **`Context` is the god object, and it lives in Core.**
   `src/Core/Context.h`: `class Context : public QObject`, constructed
   with `MainWindow *mainWindow`, holding `MainWindow * const
   mainWindow`, `AthleteTab *tab`, and `Athlete *athlete`, and
   including `RealtimeData.h`, `RideFile.h`, `Season.h`,
   `CloudDBChart.h`, and `CloudDBUserMetric.h`. It emits
   `telemetryUpdate(const RealtimeData &)`. Any core code that takes
   a `Context *` can therefore reach the GUI. This is why §2.3's
   `ErgFile(Context *)` matters: workout parsing depends on Core,
   which depends (via `Context`) on Gui types
   (`MainWindow : public QMainWindow`, `AthleteTab : public QWidget`
   in `src/Gui/`).
2. **Only five `src/Core` headers touch Widgets/Qwt at all:**
   - `GcCalendarModel.h` — constructor takes `QWidget *parent`
     (it is a `QAbstractProxyModel`).
   - `IntervalItem.h` — dialog constructors take `QWidget *`.
   - `NamedSearch.h` — `EditNamedSearches(QWidget *, Context *)`.
   - `TimeUtils.h` — `DateSettingsEdit : public QWidget`.
   - `SplineLookup.h` — includes `qwt_spline_basis.h`,
     `qwt_spline_cubic.h`, `qwt_spline_polynomial.h`. This is pure
     spline maths, but it is a Qwt dependency inside Core and must
     be replaced or vendored before any "no Qwt in core" build check
     can pass for the whole of `src/Core`.
   - Everything else in `src/Core` is Qt Core / `QObject` level.
   - `RealtimeData.h`'s `#include <QApplication>` is the same class
     of leak, in `src/Train` rather than `src/Core`.
3. **The build check must be scoped, not global, at first.**
   A check that forbids Widgets/Qwt includes across all of
   `src/Core/` would fail on day one for the five headers above and
   would block Phase 0. Recommendation: the Phase 0 build check
   applies only to the *new* Phase 0 headers (the TDD types), and is
   extended file-by-file as those five headers are cleaned up in
   later small commits. The check itself is implemented as
   `util/check-core-includes.sh` (Phase 0 step 3, committed
   separately): it greps the Phase 0 headers (seven types plus Mapping.h) for any Qt include
   (`#include <Q...`, `#include "Qt...`) or Qwt include
   (`#include "qwt...`, `#include <qwt...`), prints
   `file:line:match` on failure, and exits 1. It is stricter than
   Q1 on purpose — Q1 allows Qt Core, but these Phase 0 headers are
   pure standard C++, so any Qt include in them is a failure until
   the script is deliberately updated in the same commit that
   justifies it. It is a documented manual step for now, not wired
   into qmake or CI, to keep Phase 0 behaviour-neutral; CI wiring
   is a separate, later decision.

## 4. Phase 0 plan (small commits, in order)

Each commit below is a separate, reviewable commit on
`core-view-split-phase0`, with a message stating what it does, why,
and what still passes. No commit changes behaviour; the GUI must build
and the 8 baseline unit-test suites must pass after each one.

1. **This inventory** (this commit). Planning only.
2. **New pure-type headers, unused.** Add, under `src/Core/`:
   `TelemetrySample.h`, `DeviceInfo.h`, `Workout.h`
   (`Workout` / `WorkoutStep`), `TrainerCommand.h`
   (`TrainerCommand` + `Mode`), `SessionState.h`, `AthleteProfile.h`,
   `Clock.h` (`IClock`, `SystemClock`, `FakeClock`). Qt Core types
   (`QString`, `QDateTime`) are allowed per Q1; `std::string` is
   preferred for the device/game-facing structs (`DeviceInfo`,
   `TelemetrySample` ids) per the TDD §7.5. No existing file is
   modified in this commit except the build file list, if needed to
   compile the headers at all — prefer header-only types so no build
   change is needed.
3. **Build check for the new headers only.** Done:
   `util/check-core-includes.sh`, as described in §3.3. Usage:
   `./util/check-core-includes.sh` (checks the Phase 0 headers),
   or with explicit file arguments. Verified: passes on the
   current headers, and fails (exit 1, with file:line) on a
   deliberately bad copy containing `#include <QWidget>` and on
   one containing `#include "qwt_spline_basis.h"`, and on a
   missing file. Wire it into CI as a separate, later commit if
   the maintainers want it.
4. **Mapping unit tests, no production migration.** Done:
   `unittests/Core/phase0Mapping/` (`testPhase0Mapping.cpp`,
   `phase0Mapping.pro`, wired into `unittests/unittests.pro`),
   14 test cases, all passing alongside the 8 baseline suites.
   One design correction from the plan as written: the tests do
   NOT include the real `RealtimeData`, `ErgFile`, or `Zones`
   headers. `RealtimeData.h` includes `GoldenCheetah.h`,
   `ErgFileBase.h`, and `<QApplication>`, and its object contains
   `RealtimeDataSession`, which references `Context`, `Athlete`,
   and `RideFile`; `ErgFile.h` includes `Context.h`, `Zones.h`,
   `Settings.h`, `<QtGui>`, and `<QTextEdit>`; `Zones.h` includes
   `GoldenCheetah.h` and `Athlete.h`. Linking any of them would
   drag the God Context and the GUI into a core test, defeating
   Phase 0. Instead the tests use plain fixtures mirroring only
   the old types' field values, plus test-local mapping functions
   implementing the §2 tables: `RealtimeData` fields → sample
   (watts / HR / cadence / speed / distance / altitude / slope →
   gradient / load → trainer_load_pct / msecs → monotonic_ms,
   explicit validity flags, `watts_3s` left 0 for the session to
   compute, dropout ≠ zero watts); `ErgFileSection` → step (ERG
   value → target_watts, Slope value → target_gradient_pct);
   `ZoneRange` FTP → `AthleteProfile::ftp_watts`. The test-local
   mapping functions are prototypes: step 5 promotes them to
   `src/Core` and switches these tests to call them, with the
   assertions unchanged.
5. **Field-mapping tables, as documentation, then as code.** Done:
   `src/Core/Mapping.h` (header-only, pure std C++, listed in
   `src.pro` HEADERS and in `util/check-core-includes.sh`, which
   now checks 8 Phase 0 files). It defines three plain source
   structs and three mapping functions, and the step 4 tests now
   call them with their assertions unchanged:
   `telemetryFromSource(TelemetrySource, wallclock_ms)` →
   `TelemetrySample` (series → sample fields, explicit validity,
   `watts_3s` left 0 for the session); `workoutStepFromSection
   (WorkoutSectionSource)` → `WorkoutStep` (ERG value →
   target_watts, Slope value → target_gradient_pct);
   `athleteProfileFromSource(AthleteSource)` → `AthleteProfile`
   (caller picks the `ZoneRange`/sport for FTP, athlete settings
   for weights/height). The functions deliberately do NOT take
   `RealtimeData`/`ErgFile`/`ZoneRange` — those headers pull
   Widgets/Context/GUI — so the caller at the boundary, where the
   old types live, fills a source struct and calls the mapping.
   That boundary contract is what makes this header testable
   without linking the GUI. Two corrections are recorded in
   `Mapping.h`: (a) `ErgFileSection`, not `ErgFilePoint`, maps to
   `WorkoutStep` (points are within-section samples, Phase 1
   parser work); (b) `SessionState` is NOT mapped here, because
   trainer booleans alone underdetermine it (Running vs Paused
   needs lifecycle context) — that mapping belongs in the step 6
   adapter and stays a deferred sub-decision. Do not yet change
   `RealtimeData`, `ErgFile`, or `Context`.
6. **One call-site adapter, as the Phase 0 exit proof.**
   Pick the smallest observable seam — recommended: the
   `Context::telemetryUpdate` path — and add a thin adapter that
   converts `RealtimeData` to `TelemetrySample` at the boundary,
   without changing what the GUI renders. If that adapter cannot be
   added without changing behaviour, Phase 0 is not done; do not
   proceed to Phase 1.
7. **Phase 0 exit criteria** (all must hold before Phase 1 starts):
   - New headers compile with no Widgets/Qwt includes (check in
     step 3 passes).
   - Mapping tests pass, alongside the 8 baseline suites (9 suites
     total after step 4).
   - Existing GUI builds (`qmake build.pro -r`, `sub-qwt`,
     `sub-src`) and runs `--version` as in the baseline.
   - No behaviour change: no workout, device, recording, or chart
     logic has been altered; only new types, tests, and one adapter
     exist.
   - Open sub-decisions from §2 are recorded as decided or
     explicitly deferred: stable device id derivation, `SessionState`
     mapping, which `ZoneRange`/sport builds `AthleteProfile`, and
     which of the ~70 `RealtimeData` series are out of scope for the
     Phase 0 sample.

## 5. What Phase 0 deliberately does not do

- No `WorkoutPlayer`, no `TrainSession`, no `IDevice`, no
  `FakeDevice`, no `Recorder` — those are Phases 1–4.
- No move of `ErgFile` parsing, `RealtimeData`, or `Context`.
- No change to `src.pro`, the qmake build, or the TDD's Appendix A
  directory layout (deferred to the Q2 build-system decision).
- No change to athlete directories, workout library formats, or
  file formats.
- No removal of the current Qt Widgets GUI, and no QML, web, or
  game client — the "second client" proof belongs to Phase 5.

## 6. Sources checked

- `src/Train/RealtimeData.h`, `src/Train/RealtimeController.h`,
  `src/Train/DeviceTypes.h`, `src/Train/DeviceConfiguration.h`,
  `src/Train/ErgFile.h`, `src/Train/ErgFileBase.h`,
  `src/Train/TrainSidebar.h`, `src/Train/TrainBottom.h`
- `src/Core/Context.h`, `src/Core/Athlete.h`, `src/Core/Settings.h`,
  and a grep of all `src/Core/*.h` for Widgets/Qwt includes
- `src/Metrics/Zones.h`, `src/FileIO/RideFile.h`,
  `src/Core/RideItem.h`
- `src/src.pro` (Qt modules, Qt >= 6.5.3 requirement),
  `build.pro` (subdirs: qwt, src, unittests, ordered)
- Baseline build log and test logs in `~/workspace/` from 2026-10-30
