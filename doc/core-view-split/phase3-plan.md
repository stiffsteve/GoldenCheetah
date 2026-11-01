# Phase 3 plan: IDevice plus DeviceManager plus FakeDevice

Status: in progress, 2026-11-01. TDD section 14, Phase 3, and
section 7.5 (IDevice and ITrainer). Phase 2 is complete on
branch `core-view-split-phase0` (see `phase2-plan.md`); the
Phase 2 bus and merge function are what device samples will
flow into in Phase 4.

## What Phase 3 is, in this codebase

TDD Phase 3 says: drivers become implementations of `IDevice` /
`ITrainer`, add `DeviceManager` and `FakeDevice`. Acceptance:
headless run with `FakeDevice` produces same FIT as before for
a scripted workout, and real ANT+ and BLE devices still work
in the GUI.

Three scope decisions make that concrete here, recorded so
they can be challenged, following the Phase 1/2 pattern:

1. **No real driver is converted in Phase 3.** The legacy
   device layer is `RealtimeController` (a QObject with
   start/restart/pause/stop, find/discover, doesPush/doesPull,
   getRealtimeData/pushRealtimeData, setLoad/setGradient, and
   calibration) plus about eleven subclasses (ANTlocal, BT40,
   Computrainer, Daum, Ergofit, Fortius, Imagic, Kettler,
   KettlerRacer, Monark, Null) and `DeviceConfiguration`
   (type, name, portSpec, deviceProfile, wheelSize, and more).
   Converting even one of those to `IDevice` is a hardware-
   testing change, not a core step, and converting all of them
   is a project of its own. Phase 3 therefore delivers the
   interfaces, the fake, and the manager, with no legacy driver
   changed and no GUI device code changed. The driver conversion
   is a separate, hardware-gated project, and the headless-FIT
   acceptance (which needs a Recorder, Phase 4) is deferred
   with it, not claimed here.

2. **The headless-FIT proof is Phase 4, not Phase 3.**
   TDD Phase 3's acceptance needs a session and a recorder to
   produce a FIT file at all. Those are Phase 4. Phase 3's
   acceptance is instead: a scripted `FakeDevice`, driven by
   manual ticks with no sleep and no hardware, emits the
   scripted samples (with lag, noise, and dropout applied as
   configured), converges to ERG targets, and records the
   trainer commands it received — all asserted in unit tests.
   That is the deterministic half of the Phase 3 acceptance,
   and it is what Phase 4's session tests will build on.

3. **Devices are manually ticked, never timer-driven.**
   TDD 15.4 says: do not add a Qt timer to `FakeDevice` just
   to make tests pass; manual ticking is the feature. So
   `FakeDevice::tick(now_ms)` is the only way samples are
   produced, and `IDevice::start()`/`stop()` gate it. There is
   no thread, no Qt timer, and no global clock in core device
   code. A future session (Phase 4) calls `tick()` on its own
   schedule; a GUI adapter (later) can call it from a timer,
   but core does not own that timer.

## Inventory (docs-only, step 0)

- `RealtimeController` (`src/Train/RealtimeController.h`):
  QObject, push vs pull devices, `find()`/`discover()` for
  presence, `getRealtimeData()`/`pushRealtimeData()` for
  telemetry into a mutable `RealtimeData`, `setLoad()`/
  `setGradient()`/`setMode()` for trainer control, calibration
  getters/setters. Its subclasses are listed in decision 1.
- `DeviceConfiguration` (`src/Train/DeviceConfiguration.h`):
  type (int), name, portSpec, deviceProfile (ANT ids), wheel
  size, inertial moment, stride length, post-processing, and
  a `RealtimeController*` pointer. Pairing configuration today
  is a list of these, serialised by `DeviceConfigurations`.
- Phase 0 `DeviceInfo` (`src/Core/DeviceInfo.h`): id (stable
  string, never a pointer), name, transport, and capability
  flags (controllable trainer, power, HR, speed/cadence).
  Phase 3 closes the deferred stable-id question for fake
  devices only: a fake device's id is `fake:<name>`, stable
  across runs for the same name. Legacy derivation (type plus
  portSpec, or something else) is NOT decided here, because
  portSpec can change and is not truly stable; that decision
  belongs to the future legacy adapter, not to Phase 3.

## Steps (each a small commit, with verification)

1. **Interfaces** (`src/Core/IDevice.h`, header-only, pure
   std C++): `ISampleSink` (onSample with device_id and
   sample, per TDD 7.5), `IDevice` (info, connect, disconnect,
   isConnected, start, stop, setSampleSink), and `ITrainer`
   (setErgMode, setSlopeMode, setResistanceMode, setSimMode).
   No Qt types in any signature (std::string, double, bool),
   no widget callbacks, no ownership transfer in the
   interfaces themselves — ownership is the manager/session's
   job (decision in step 3). `DeviceInfo` is reused as-is
   from Phase 0; it is not redefined here.

2. **FakeDevice** (`src/Core/FakeDevice.h`, header-only,
   pure std C++): implements `ITrainer`. Configured with a
   script (a list of samples with monotonic times), a lag,
   a deterministic noise amplitude and seed (a small LCG, not
   std::rand, so tests are reproducible), dropout intervals
   (during which no sample is emitted), and an ERG time
   constant. `tick(now_ms)` emits script samples whose
   script time plus lag is at or before now, skipping dropout
   intervals, with noise applied to watts and HR. When an ERG
   target is set, reported watts converge toward it by a
   first-order model (factor 1 - exp(-dt/tau)), never
   overshooting, and the last command of each kind is recorded
   and readable for tests. Slope mode records the gradient
   and reports it on generated samples. Connect/start gate
   ticking: a disconnected or stopped device emits nothing.
   Simplifications, documented in the header: noise is
   uniform, not physiological; slope speed is a simple linear
   model, not physics; a script and an ERG target interact by
   the ERG value overriding script watts when a target is
   active, which is the behaviour a session test needs.

3. **DeviceManager** (`src/Core/DeviceManager.h`, header-
   only, pure std C++): discovery separate from connection.
   `registerDevice(info, factory)` registers a known device
   and its factory; `scan()` returns the registered
   `DeviceInfo` list without connecting or creating anything;
   `open(id)` creates a fresh device via the factory and
   returns it as a `unique_ptr` (caller owns it, per TDD Q6:
   the session owns devices passed to it, the manager owns
   discovery, not devices); `open()` of an unknown id returns
   nullptr. A `registerFake()` helper registers a fake device
   with a given name and script, so tests and Phase 4 can get
   a fake from the manager without hand-wiring a factory.
   No real hardware discovery in Phase 3: `scan()` returns
   only registered devices, and that limitation is stated in
   the header.

4. **Unit tests** (`unittests/Core/fakeDevice/`, new QtTest
   suite): interface conformance (info, connect/disconnect/
   start/stop gating, sink null safety), script emission in
   order, lag (a sample is not emitted before script time
   plus lag), dropout (no sample during a dropout interval,
   samples resume after it), deterministic noise (same seed
   gives the same output, zero noise gives exact script
   values), ERG convergence (watts approach the target within
   tolerance and never overshoot, and a target change
   re-converges), slope/resistance/sim command recording,
   and manager tests (scan lists registered devices without
   connecting, open returns an independent device per call,
   open of an unknown id is nullptr, manager still lists a
   device after it has been opened). No `sleep()`; all times
   are explicit tick parameters.

5. **Exit check**: `util/check-core-includes.sh` passes on
   the Phase 2 files plus the three new core headers; GUI
   builds and `--version` matches the Phase 2 exit (V3.8,
   Qt 6.8.3, 382 metrics, DB schema 160); all suites pass
   (baseline 8 + mapping + workoutPlayer + telemetryBus +
   fakeDevice); no GUI behaviour change (no GUI, driver, or
   controller file changed; only build-file lists gain the
   new headers). Phase 3 is done when all of these hold.

## What Phase 3 deliberately does not do

- No conversion of any legacy `RealtimeController` subclass
  to `IDevice`, no ANT/BLE driver work, no GUI device UI change.
- No headless CLI and no FIT comparison (needs Phase 4
  TrainSession and Recorder).
- No legacy `DeviceConfiguration` to `DeviceInfo` adapter
  (stable legacy id derivation is undecided, see above).
- No threading, no Qt timers, no global clock in core device
  code, no live metrics, no recording.
