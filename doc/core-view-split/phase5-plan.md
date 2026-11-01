# Phase 5 plan: Thin TrainView

Status: COMPLETE, 2026-11-01, as scoped below. TDD section
14, Phase 5, and section 14.2 (second client proof). Phase
4 is complete on branch `core-view-split-phase0` (see
`phase4-plan.md`).

## Exit check, verified 2026-11-01

- GUI build: qmake exit 0, `make sub-src` exit 0, binary
  27,495,832 bytes (Nov 1 14:45, up about 32 KB from the
  adapter object) and `--version` matches Phase 4 exit
  (V3.8 5015, Qt 6.8.3, QWT 6.8.0, GCC 13.3.0, DB schema
  160, 382 metrics).
- `util/check-core-includes.sh`: PASS on 18 files (the
  adapter is GUI-layer, not core, so it is not added to
  that list, deliberately).
- Tests: new trainSessionAdapter suite 7 passed (5 tests
  plus init/cleanup), all earlier suites with baseline
  totals — 14 suites total, all passing. The qmake test
  build first failed to link the adapter suite (moc for
  TrainSessionAdapter.h was not generated in the test
  build); fixed by listing the adapter header in the test
  .pro HEADERS, with the reason recorded there.
- Dashboard proof verified separately: built manually with
  g++, ran 65 s, printing step 0 then step 1, manual sent
  then cleared, and final live metrics consistent with the
  manual period.
- No TrainSidebar, TrainBottom, plot, or controller file
  changed, so no GUI behaviour change.

Commits on `core-view-split-phase0`, after Phase 4's
e8d4a9aaa: 39bde7525 (adapter), fa463d721 (dashboard),
plus the test commit below. No production GUI code uses
the adapter yet; the TrainSidebar switch, the QML/Godot
second-client form, and FIT ordering remain gated
decisions for the user, as listed under "What Phase 5
deliberately does not do".

## What Phase 5 is, in this codebase — and what it is not

TDD Phase 5 says: `TrainView` becomes layout plus Qwt plus
buttons plus observer adapter. Acceptance: the user cannot
tell from behaviour, except bugs go down, and a second tiny
client works.

Two facts from the inventory (step 0, below) change what
that means here, and they are recorded first, because they
decide the scope:

1. **There is no single TrainView to thin.** v3.8 has no
   `TrainView` file. The train cluster is `TrainSidebar`
   (3,528 lines, `src/Train/TrainSidebar.cpp`), `TrainBottom`,
   `RealtimePlotWindow`, and the workout windows, plus a
   `TrainView` class that is only an `AbstractView` in
   `src/Gui/Views.h`. `TrainSidebar` alone touches
   `RealtimeData`, `ErgFile` (including laps, text cues,
   and slope/CRS routes), eleven `RealtimeController`
   subclasses, the athlete database, video, and the plots.
   Thinning it in one step, without driver conversion
   (deferred as hardware-gated since Phase 3) and without
   hardware to regression-test against, would be a blind
   rewrite of the most behaviour-sensitive GUI in the
   product. It is not a step that can be verified headless,
   and it is not a step this phase claims.

2. **The second-client form is still the user's decision.**
   A Phase 5 plan was presented in the main chat on
   2026-11-01 asking the user to pick a minimal QML
   dashboard or a small Godot plugin, and to say whether
   FIT conversion comes before, alongside, or after Phase 5.
   The user has now said "Keep going with Phase 5, as
   before" in this chat, which approves Phase 5 work under
   the established pattern, but it does not pick QML or
   Godot, and it does not answer the FIT ordering question.
   Phase 5 therefore does not assume either answer. FIT
   stays a separate gated IO step, as in Phase 4, and the
   second-client proof below is form-neutral: it proves a
   second client can subscribe and send commands through
   the public session API without reaching into internals,
   without pre-empting the QML/Godot choice, which remains
   open for the user to make.

## Scope decisions for this phase, following the pattern

1. **Phase 5 delivers the observer adapter and the second-
   client proof, not the TrainSidebar switch.** The adapter
   is the TDD "observer adapter" for Phase 5: a Qt object
   that implements `ISessionObserver` and re-emits Qt
   signals, and drives `TrainSession::tick()` from a QTimer
   in production while remaining manually tickable in tests.
   TrainSidebar is NOT modified in Phase 5. The switch of
   TrainSidebar (and the plots) to the adapter and the
   session is the gated GUI step that follows, once driver
   conversion and a hardware regression comparison exist.
   Claiming "TrainView is thin" while TrainSidebar is
   untouched would be false; this plan claims the seam, not
   the switch, and says so plainly.

2. **The adapter is GUI-layer, not core.** It lives in
   `src/Train/`, uses Qt (QObject, QTimer, signals), and is
   NOT added to `util/check-core-includes.sh`, which checks
   core headers for Qt. Core stays pure; the adapter is
   where Qt is allowed, by design. The adapter does not own
   the session, the clock, the recorder, or the devices —
   the caller does, as in Phase 4 — so it can be attached
   to an existing session without changing its lifecycle.

3. **Tests stay deterministic: manual ticking, no event
   loop.** The adapter's QTimer is for production only.
   Tests drive the adapter with an explicit `tick()` call,
   exactly as core tests drive `TrainSession::tick()`, and
   assert signals with QSignalSpy. No `sleep()`, no event
   loop, no GUI window in the adapter tests.

## Inventory (docs-only, step 0)

- `TrainSidebar` (`src/Train/TrainSidebar.h/.cpp`, 3,528
  lines): `GcWindow`, holds a `Context*`, a tab widget,
  device list, and `updateData(RealtimeData&)` for push
  devices. It creates `RealtimeDataSession` objects, reads
  `ErgFile` (including `Texts` cues at line 2103), manages
  controllers via `Devices[dev].controller->getRealtimeData`,
  and calls `context->notifyTelemetryUpdate(rtData)` at
  line 2177 to signal everyone to update telemetry. That
  call is the Phase 0/2 seam: `Context` already stores a
  `TelemetrySample` and publishes it on the bus there.
- `TrainView` (`src/Gui/Views.h:114`): `AbstractView`,
  not a train implementation. There is no file to thin
  under that name.
- `TrainBottom` (543 lines), `RealtimePlotWindow` (385
  lines): the other train windows, also `RealtimeData`-
  driven, also untouched in Phase 5.
- Legacy controllers: eleven `RealtimeController`
  subclasses (Phase 3 inventory), none converted to
  `IDevice`. A GUI session driven by real hardware still
  needs that conversion, which is why the TrainSidebar
  switch is gated, not done here.

## Steps (each a small commit, with verification)

1. **Qt adapter** (`src/Train/TrainSessionAdapter.h/.cpp`,
   GUI-layer, Qt): `TrainSessionAdapter : public QObject,
   public ISessionObserver`. Constructor takes a
   `TrainSession&` (not owned) and subscribes itself.
   Signals: `telemetryUpdated(const TelemetrySample&)`,
   `sessionStateChanged(SessionState)`,
   `workoutStepChanged(int)`. Slots: `tick()` (calls
   session tick), `startTicking(int interval_ms)` (starts
   a QTimer that calls tick), `stopTicking()`, plus
   pass-through slots for session intents (start, pause,
   resume, stopAndSave, lap, loadWorkout, setManualCommand,
   setSimGradient) so a view can talk to the adapter
   without holding the session. `TelemetrySample` and
   `SessionState` are declared with Q_DECLARE_METATYPE so
   signals can be spied and queued. The adapter does not
   touch widgets, plots, or `RealtimeData`.

2. **Second client proof** (`src/Core/gc-train-dashboard.cpp`,
   standalone, NOT wired into qmake, built manually with
   g++): a console dashboard that runs a workout with a
   `FakeDevice`, subscribes an observer that prints
   telemetry, live metrics, and step changes to stdout,
   and sends a manual ERG command mid-workout, all through
   the public `TrainSession` API and `ISessionObserver`.
   It reaches into no internals (no player, no recorder
   internals, no device internals beyond the public
   `FakeDevice` tick the harness already uses). This is
   the TDD 14.2 proof in form-neutral form: if this
   200-line client works, the seam is real; the QML or
   Godot form is a later choice, not assumed here.

3. **Unit tests** (`unittests/Train/trainSessionAdapter/`,
   new QtTest suite): adapter signals fire on session
   telemetry/state/step events (via QSignalSpy, manual
   tick, no event loop), adapter slots forward intents to
   the session (start/pause/resume/lap/manual), adapter
   unsubscribe on destruction does not break the session,
   and the dashboard client runs a short workout and
   prints a manual-override step (verified by running it,
   not by a QtTest, because it is a standalone program).
   No `sleep()`.

4. **Exit check**: GUI builds (the adapter is compiled
   into `src`, so `make sub-src` must pass) and `--version`
   matches the Phase 4 exit (V3.8, Qt 6.8.3, 382 metrics,
   DB schema 160); `util/check-core-includes.sh` passes on
   the Phase 4 files (the adapter is not a core header, so
   it is not added to that list, and that exclusion is
   deliberate); all suites pass (baseline 8 + mapping +
   workoutPlayer + telemetryBus + fakeDevice + trainSession
   + trainSessionAdapter); no TrainSidebar, plot, or
   controller file changed, so no GUI behaviour change.
   Phase 5 is done when all of these hold, with the
   TrainSidebar switch explicitly remaining as gated work.

## What Phase 5 deliberately does not do

- No TrainSidebar, TrainBottom, plot, or controller
  modification, and no GUI behaviour change.
- No legacy `RealtimeController` conversion, no ANT/BLE
  driver work.
- No FIT writer or `RideFile` conversion (separate gated
  IO step, ordering still the user's decision).
- No QML dashboard and no Godot plugin (form still the
  user's decision; the dashboard proof is console-only
  and form-neutral).
- No daemon mode (deferred Q4), no threads in core, no
  Qt timers in core tests.
