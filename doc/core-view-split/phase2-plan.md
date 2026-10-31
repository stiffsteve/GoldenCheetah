# Phase 2 plan: RealtimeData to TelemetrySample plus TelemetryBus

Status: in progress, 2026-10-31. TDD section 14, Phase 2, and
section 7.7 (observer fan-out, in the TrainSession description).
Phase 1 is complete on branch `core-view-split-phase0` (see
`phase1-plan.md`); Phase 0's adapter already stores a
`TelemetrySample` at the `Context` boundary, which is what this
phase fans out.

## What Phase 2 is, in this codebase

TDD Phase 2 says: move shared mutable realtime data to immutable
`TelemetrySample` plus a bus / observer fan-out. Acceptance: old
plots still render, now from bus. No view mutates engine state.
Dropout handling is explicit.

Three scope decisions make that concrete here, recorded so they
can be challenged:

1. **The GUI is NOT switched to the bus in Phase 2.** The
   literal acceptance ("old plots still render, now from bus")
   would mean switching ten Qt signal consumers at once —
   DialWindow, ElevationChartWindow, HtmlTrainingBridge,
   LiveMapWebPageWindow, RealtimePlotWindow, SpinScanPlotWindow,
   VideoWindow, WorkoutPlotWindow (via ergPlot), WorkoutWidget,
   and WorkoutWindow — each of which reads far more than the 14
   `TelemetrySample` fields (see the inventory below). That is a
   behaviour-risk change, not a Phase 2 core step, and it was
   already agreed as a separate gated decision when Phase 2 was
   proposed. Phase 2 therefore delivers the bus, the merge/
   dropout rules, and a dual-running boundary adapter (the bus
   is fed alongside the existing Qt signal, with zero subscribers
   in production), and the pilot consumer switch is a separate
   decision requiring its own go-ahead. Following the Phase 1
   pattern, "GUI still works via adapter" is satisfied by
   providing the adapter, not by switching the GUI.

2. **Merging is per-field by validity, not by device type.**
   The TDD's TrainSession tick merges "latest samples per device
   into one TelemetrySample. If power comes from one device and
   HR from another, both survive." Phase 2 implements exactly
   that merge as a pure function, before TrainSession exists:
   power comes from the newest power-valid source, HR from the
   newest HR-valid source, and the remaining fields from the
   newest sample overall. A source that has not been heard from
   within the timeout is stale and contributes no valid field.
   Dropout is then explicit by construction: a merged sample
   with `power_valid == false` is a dropout, and its `watts`
   field keeps the last known value — it is never zeroed, because
   "dropout is not zero watts, it is invalid data" (Phase 0
   TelemetrySample rules). Consumers must gap on invalid, not
   plot zero.

3. **No device work, no TrainSession, no live metrics.**
   `IDevice`/`FakeDevice`/`DeviceManager` are Phase 3, and
   TrainSession/Recorder/live metrics (3s power, NP, W'bal, TSS)
   are Phase 4. Phase 2 does not invent a device layer, does not
   compute derived metrics, and does not decide a device timeout
   for production — the merge timeout is a parameter, and the
   production value (for ~1 Hz devices, 3 s is three missed
   samples) is a Phase 3/4 decision, not a Phase 2 constant.

## Inventory (docs-only, step 0)

`RealtimeData` (`src/Train/RealtimeData.h`) is a mutable bag:
about 70 setters (`setWatts`, `setHr`, `setCadence`, `setSpeed`,
`setDistance`, `setAltitude`, `setSlope`, `setLoad`, plus
derived/advanced series) and about 60 `dataseries` values
(Watts, Speed, Cadence, HeartRate, Load, XPower, BikeScore, RI,
Joules, SkibaVI, IsoPower, BikeStress, IF, VI, Wbal, SmO2, tHb,
pedal metrics, latitude/longitude, temperature, VAM, and more).
Producers mutate it (ANT drivers, Train code, virtual power)
and then `Context::notifyTelemetryUpdate` emits it by Qt signal.
Consumers of `Context::telemetryUpdate(RealtimeData)` are the ten
windows listed in decision 1 above, plus `Context::telemetrySample()`
(Phase 0, no readers yet). `TelemetrySample` covers only 14 of
those series; everything else (derived/advanced series, laps,
position, temperature, pedal metrics, averages) is out of scope
for the bus in Phase 2, exactly as it was out of scope for the
Phase 0 adapter. A bus consumer that needs those series must
keep using the legacy signal until a later phase covers them.

## Steps (each a small commit, with verification)

0. **Inventory** (this doc, plus a short inventory section in
   `phase2-plan.md` itself): readers, mutators, signal consumers,
   and out-of-scope series, as above. No code.

1. **TelemetryBus** (`src/Core/TelemetryBus.h`, header-only,
   pure std C++): `ITelemetryObserver` with one method,
   `onTelemetry(const TelemetrySample&)`, plus `TelemetryBus`
   with subscribe/unsubscribe, publish (synchronous, in
   subscription order, to a copy of the observer list so that
   unsubscribing during publish does not break iteration),
   latest (hasLatest + last sample), and observerCount.
   Samples are passed by const reference and copied by the bus
   into latest; an observer mutating its received copy cannot
   affect the bus or other observers, because there is no shared
   mutable sample — that is the Phase 2 immutability guarantee,
   and it is tested. Lifetime rule, documented in the header:
   an observer must unsubscribe before it is destroyed, and must
   not delete itself or another observer during publish; the bus
   holds raw pointers, not ownership, matching the TDD's
   subscribe/unsubscribe API. This bus observer is deliberately
   not the TDD 7.7 session observer (which also has
   onSessionStateChanged/onWorkoutStepChanged); that session
   observer belongs to TrainSession in Phase 4.

2. **Merge and dropout** (`src/Core/TelemetryMerge.h`,
   header-only, pure std C++): `mergeSources(samples, now_ms,
   stale_timeout_ms)` as a pure function, per decision 2.
   Stale means `now_ms - received_ms > stale_timeout_ms`
   (strictly greater; exactly at the timeout is still fresh).
   If no power-valid source is fresh, merged `power_valid` is
   false and `watts` keeps the newest power value seen (stale or
   not), never zero. Same for HR. If no sample at all is fresh,
   the merged sample is still produced from the newest sample,
   with both validity flags false — a dropout sample, not an
   absence. Monotonic/wallclock come from the newest sample.
   `watts_3s` is never set by merging (it is a Phase 4 derived
   metric, per the TelemetrySample rules).

3. **Boundary adapter, dual-running** (`src/Core/Context.h`
   only): add a `TelemetryBus` member to `Context`, publish
   `lastTelemetrySample` to it in `notifyTelemetryUpdate` after
   storing it and before emitting the existing Qt signal, and
   expose it via `Context::telemetryBus()`. No existing signal,
   slot, or plot changes; in production the bus has zero
   subscribers, so publish is a no-op loop plus a stored latest
   sample. This is the Phase 0 adapter pattern, extended from
   "store one sample" to "fan it out".

4. **Unit tests** (`unittests/Core/telemetryBus/`, new QtTest
   suite): bus fan-out to multiple observers in order,
   unsubscribe, latest before/after publish, observer mutation
   isolation, zero-observer publish; merge tests for power-from-A
   plus HR-from-B both surviving, newest power source winning,
   power dropout (stale A, fresh B with no power -> power_valid
   false, watts last known, not zero), HR dropout, all-stale
   dropout, and the timeout boundary. No `sleep()`; all times are
   explicit parameters, like the Phase 1 player tests.

5. **Exit check**: `util/check-core-includes.sh` passes on the
   Phase 1 files plus the two new core headers; GUI builds and
   `--version` matches the Phase 1 exit (V3.8, Qt 6.8.3, 382
   metrics, DB schema 160); all suites pass (baseline 8 +
   mapping + workoutPlayer + telemetryBus); no GUI behaviour
   change (no plot, widget, or window file changed; the only
   GUI-adjacent change is `Context` publishing to an empty bus).
   Phase 2 is done when all of these hold, not when the code
   merely compiles.

## What Phase 2 deliberately does not do

- No switching of any plot/widget/window to the bus (separate
  gated pilot decision).
- No IDevice/FakeDevice/DeviceManager (Phase 3), no TrainSession/
  Recorder/live metrics (Phase 4), no TrainView thinning (Phase 5).
- No derived/advanced `RealtimeData` series on the bus, no laps,
  no text cues, no position/temperature/pedal series.
- No production timeout constant, no threading, no Qt signals in
  core (the bus is synchronous and pure std; Qt adaptation, if
  needed for a pilot consumer, is a Train-side adapter in that
  pilot, not in core).
