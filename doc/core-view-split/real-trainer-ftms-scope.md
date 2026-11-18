# Real trainer: FTMS adapter (headless part)

Scope for the first real-driver conversion in the core/view split.
Branch `core-view-split-phase0`. Authorised 2026-11-18
("Both, BicycleSim first"; BicycleSim done, fork tip `3f93c6d7c`).

This item builds the **headless part only**: protocol tests, the
adapter, the calibration extension, adapter tests, docs, and the
exit check. This VM has no Bluetooth hardware
(`/sys/class/bluetooth` absent), so no discovery, pairing, radio,
or trainer test can run here. The hardware part — discovery
against a real trainer, ERG convergence, spin-down end to end,
soak, hardware regression comparison — is a separate step
afterwards, on a Bluetooth-capable host with the user's Zwift Hub
(FTMS over Bluetooth, also ANT+ FE-C; it already works well with
stock GoldenCheetah, so the legacy BT40 path is the proven
baseline for the regression comparison).

## 1. Driver choice: FTMS over BLE, via the BT40 stack

FTMS (Bluetooth Fitness Machine Service, UUID 0x1826) is the
current standard: every smart trainer sold since ~2018 speaks it.
The protocol code already exists (`Ftms.h`: control-point command
set, feature/target parsing, indoor-bike data parsing, power and
resistance capping), and it is the only first driver that
exercises the full `ITrainer` surface (ERG, slope/sim, resistance,
spin-down calibration). Computrainer is the smallest proof but
discontinued; ANT+ FE-C is the second modern standard (~8,000
lines, do second); Kettler is ERG-only and rare.

## 2. Adapter, not a native rewrite

`Bt40DeviceAdapter` in `src/Train/`, implementing core
`ITrainer` (+ core `ICalibration`, §5) over the existing
`BT40Controller`/`BT40Device`. The legacy controller stays
functionally untouched (strangler pattern): GATT handling, FTMS
encoding, and the proprietary load types (Tacx UART, Wahoo KICKR,
Kurt) are reused, not re-implemented. The GUI keeps working
throughout; the TrainSidebar switch is a separate gated step
(it needs this driver converted **plus** a hardware regression
comparison).

### Backend seam

The adapter talks to a small backend interface, `IBt40Backend`,
defined in the adapter header with plain (std) types:

- `connect()` / `disconnect()` / `isConnected()` — transport
- `telemetry()` — latest snapshot (`Bt40Telemetry`: watts,
  speed_kph, cadence, heartrate)
- `setErg(watts)` / `setSlope(gradient, weight)` /
  `setSim(gradient)` / `setResistance(level_0_1)` — commands
- `requestCalibration()` / `resetCalibration()` + the six
  calibration getters — calibration (§5)

The production backend wraps a `BT40Controller` (created by the
adapter with a default `DeviceConfiguration`, parent `nullptr`)
as telemetry hub and parent, plus **one `BT40Device` created
directly for the scanned target** (`QBluetoothDeviceInfo` captured
at scan time). The device's `parent` must be a `BT40Controller`
— `updateValue`/`deviceDisconnected` call back into it via
`dynamic_cast<BT40Controller*>` — so the controller exists, but
the adapter does not use the controller's discovery/`addDevice`
path.

Why not the controller's discovery path: `addDevice` connects a
device only when a `DeviceConfiguration` is present, and it
filters through the `deviceProfile` allow-list
(`"name;address;uuid"`), whose `uuid` semantics on Linux are
uncertain (empty uuid ⇒ allow-list empty ⇒ *all* discovered
devices connect, and the controller aggregates every device's
telemetry into one `RealtimeData`). The direct-device shape gives
exact single-target control and does not depend on that question.
The controller's `devices` list stays empty; the backend calls
the device methods directly and manages the device lifetime
(disconnect + delete on stop/disconnect).

Tests use a stub backend implementing the same interface —
deterministic, no radio, no Qt event loop.

### Minimal additive legacy changes (zero behaviour change)

These expose state the adapter needs; none alters the legacy path:

- `BT40Device`: a link-state flag, set in `deviceConnected()` /
  cleared in `deviceDisconnected()`, exposed as `isConnected()`.
  (The existing `connected` flag is *intent*, not link state: it
  stays true across an inadvertent disconnect while the 5 s
  reconnect timer runs, so it cannot drive the session's
  `PausedWithError` detection.)
- `BT40Device::setLoadIntensity(double)` made public, and a
  `BT40Controller::setResistance(double level_0_1)` forwarder.
  (There is no public resistance entry today: `setLoadLevel` and
  `setLoadIntensity` are private and have no callers anywhere.)
- `BT40Device::resetCalibrationState()` (one line, forwards to
  its `CalibrationData`) and a `BT40Controller` override
  forwarding to the first supporting device, matching the
  existing calibration-getter pattern.
- `BT40Controller::isConnected()` — true if any device's link is
  up (the adapter's controller holds exactly one device).
- FTMS encoder helpers in `Ftms.cpp` (§3) and a feature-parse
  helper — behaviour-identical extractions of inline
  `QDataStream` code, so the bytes can be unit-tested.
- `CalibrationData.h/.cpp` moved `src/Train/` → `src/Core/`
  (§5); the only change is dropping the `<QtDebug>` include and
  its `qDebug()` logging lines (logging only).

The adapter never connects the controller's `setNotification`
signal (no toasts), and it does not surface the device's 5 s
auto-reconnect timer: a dropped link is `isConnected() == false`
→ the session goes `PausedWithError`; `resume()` reconnects.
That is a deliberate behaviour difference from the legacy GUI,
which silently retries.

## 3. FTMS protocol facts (verified against the code)

- `ftms_parse_indoor_bike_data` parses flags then conditional
  fields. **Correction to the scoping report:** it *discards*
  the 24-bit total distance (reads it into dummies) — the legacy
  path never forwards distance from FTMS. `updateValue`'s FTMS
  indoor-bike branch forwards only watts (`inst_power`),
  cadence (`inst_cadence / 2.0`), and speed (`inst_speed / 100`,
  only when MORE_DATA is false). HR arrives on the separate
  Heart Rate Measurement characteristic, not the indoor-bike one.
  The adapter therefore accumulates `distance_m` itself from
  speed × dt (see §4).
- Command bytes (all `QDataStream`, little-endian, control-point
  characteristic): target power = opcode `0x05` + `qint16`
  watts, capped by `ftms_power_cap` to the device's reported
  [min, max] with its increment; sim params = opcode `0x11` +
  wind `qint16` (0.001 m/s), grade `qint16` (0.01 %),
  crr `quint8`, cw `quint8`; resistance = opcode `0x04` +
  `qint16`; spin-down = opcode `0x13` + `quint8`
  (`FTMS_SPIN_DOWN_START` = 1). The encoders are extracted as
  pure helpers (`ftms_encode_*` in `Ftms.cpp`) with the exact
  same byte layout, so the suite can assert exact bytes.
- **Known legacy quirk, reproduced exactly, not fixed here:**
  sim params compute `quint8 crr = rollingResistance` and
  `quint8 cw = windResistance` — the doubles (defaults 0.0033
  and 0.6) truncate to 0, so the trainer always receives
  crr = 0, cw = 0. The FTMS units are 0.0001 and 0.01 kg/m, so
  the correct values would be 33 and 60. Fixing it changes what
  the trainer receives; it is a hardware-validation item, not
  part of this port.
- Feature characteristic payload = `quint32` features +
  `quint32` target settings; the target-settings bits drive the
  `supports_*` flags in `FtmsDeviceInformation` and (in legacy)
  trigger reads of the power/resistance range characteristics.
  The pure parse is extracted as `ftms_parse_feature`; the
  range reads stay in `updateValue`.
- Spin-down: `setMode(RT_MODE_CALIBRATE)` sends spin-down start
  (when supported); the control-point response carries the
  target-speed window (low/high, 0.01 kph) → `CalibrationData`
  target speed + state STARTED; the status characteristic
  reports STOP_PEDALING → COAST, SUCCESS → SUCCESS,
  ERROR → FAILURE. The adapter only *exposes* this state
  (§5); the calibration dialog is follow-on work, not this item.
- Hardening (only behaviour change in a previously-crashing
  case): `ftms_power_cap` / `ftms_resistance_cap` divide by the
  device's reported increment, which is 0 until the range
  characteristic is read — the first ERG/resistance command can
  arrive before that read completes. Both now pass the value
  through unclamped when the increment is 0 instead of
  dividing by zero.
- BLE sample rate ~1 Hz; the session merges on 4 Hz ticks with
  a 3 s stale timeout. Altitude is not reported by FTMS — it
  stays 0 (the app/course owns altitude).

## 4. Adapter behaviour

Lifecycle (matches `IDevice` + `FakeDevice` semantics):
`connect()` initiates the GATT connection to the scanned target
and returns whether a local BT adapter is present
(`BT40Controller::find()`); it is idempotent — reconnecting an
already-connected device returns true. `start()` gates sample
emission. `stop()` ends emission; `disconnect()` ends the
transport (tears the device down). `isConnected()` is the link
flag (§2), not the intent flag.

Pump — queue-and-drain, no core change, no mutex: the adapter
exposes `tick(int64_t now_monotonic_ms)`, called by the session
owner on the session thread before the session tick (the same
shape as `gc_session_tick` ticking device then session). Each
tick polls the backend's latest snapshot and emits one
`TelemetrySample` via the sink while started and connected.
The BT thread only mutates the controller's `RealtimeData`, as
it does in legacy; all sample emission happens on the session
thread. No push-from-thread + mutex (TDD 8.1 end state) — that
is a separate core change, deferred.

Sample mapping (from the snapshot):

| Field | Value |
|---|---|
| watts / cadence / speed_kph / heartrate | snapshot values (note: `RealtimeData` truncates watts, cadence, HR to int) |
| power_valid | connected && started |
| hr_valid | heartrate > 0 (0 means no HR data) |
| trainer_connected | connected |
| distance_m | accumulated by the adapter: speed × dt since the last emitted sample (first tick: 0) |
| altitude_m | 0 (FTMS does not report it) |
| gradient_pct | last commanded gradient |
| target_watts | last ERG target (0 otherwise; the session overwrites it in the merge anyway) |
| monotonic_ms | the tick's `now_monotonic_ms`; wallclock_ms from the system clock |

Command translation (no command-priority logic — the session
decides one `TrainerCommand` per tick; the adapter translates):

| Session command | Adapter → backend |
|---|---|
| Erg(watts) | `setMode(RT_MODE_ERGO)`, `setLoad(watts)` → `setLoadErg`, capped target power |
| Slope(gradient, weight) | `setMode(RT_MODE_SLOPE)`, `setWeight(weight)`, `setGradient(gradient)` → sim params |
| Sim(gradient, speed) | `setMode(RT_MODE_SLOPE)`, `setGradient(gradient)` with the cached weight (default 80 kg; the session's Sim carries no weight) |
| Resistance(0–100) | `setResistance(level / 100.0)` → `setLoadIntensity`: resistance = (max−min)·level + min, capped by `ftms_resistance_cap`. Mapping decision: linear across the device's reported range, documented here. |
| Idle | nothing sent |

Commands are forwarded on every session tick (no dedupe):
the session re-sending every tick *is* the resend mechanism
against a lost BLE write, and dedupe would defeat it. Cost is
4 Hz control-point writes vs the legacy GUI's 1 Hz — a
hardware-validation watch item. Commands on a disconnected
trainer record nothing (per `IDevice` rules).

Disconnect: `isConnected() == false` → the session's tick
detects it (trainer was connected, now is not) and goes
`PausedWithError` with the recorder kept open; `resume()`
calls `connect()` + `start()`.

Known limitation of the pull model (same as legacy): a trainer
whose link stays up but stops sending notifications keeps
producing identical snapshots, which the adapter keeps emitting
as fresh — a frozen trainer is masked until the link itself
drops. The legacy GUI has the same behaviour (it displays the
last values). A push-based adapter with notification timestamps
would fix it; that is future work, not this item.

## 5. ICalibration extension (core, TDD Q10)

New core header `src/Core/ICalibration.h`, pure std C++,
mirroring `RealtimeController`'s calibration virtuals in pure
types (no `setCalibrationState` — it is an ANT-only virtual;
BT40 ignores it, and the FTMS request path is `setMode`):

```cpp
class ICalibration {
    virtual ~ICalibration() = default;
    virtual uint8_t  calibrationType() const = 0;        // CALIBRATION_TYPE_*
    virtual uint8_t  calibrationState() const = 0;       // CALIBRATION_STATE_*
    virtual double   calibrationTargetSpeed() const = 0;  // kph
    virtual uint16_t calibrationSpindownTime() const = 0;
    virtual uint16_t calibrationZeroOffset() const = 0;
    virtual uint16_t calibrationSlope() const = 0;
    virtual void     requestCalibration() = 0;  // FTMS: setMode(RT_MODE_CALIBRATE) → spin-down start
    virtual void     resetCalibrationState() = 0;
};
```

The `CALIBRATION_TYPE_*` / `CALIBRATION_STATE_*` constants come
from `CalibrationData.h`, moved to `src/Core/` (§2). The
adapter implements `ICalibration` alongside `ITrainer`
(`dynamic_cast<ICalibration*>`); the backend forwards the
getters to the controller/device and `requestCalibration()` to
`setMode(RT_MODE_CALIBRATE)`. If this shape ever needs to change
materially, that is a new design decision, not part of this item.

## 6. DeviceManager and the scanner

`DeviceManager` (core) is unchanged: `scan()` stays
synchronous and returns registered devices; `open(id)` creates
a fresh device via the registered factory. The gap is filled in
the Train layer by `Bt40Scanner::discover(DeviceManager &,
int timeout_ms)`: a blocking BLE scan (local
`QEventLoop`, deadline timer) that registers each discovered
FTMS-advertising LE device (`serviceUuids` contains 0x1826)
as `DeviceInfo{id: "ble:<address>", name, transport: "ble",
is_controllable_trainer: true, has_power: true,
has_speed_cadence: true, has_hr: false}` with a factory that
builds a `Bt40DeviceAdapter` bound to that device's
`QBluetoothDeviceInfo`. Stable id `ble:<address>` closes the
Phase 3 deferred question for this driver (on macOS there is no
address — the legacy code falls back to the device UUID; this
item targets Linux, where the address is the stable id).

The scanner's pure parts are unit-testable headlessly
(FTMS-advertisement filter, `ble:<address>` id mapping); the
`QEventLoop` discovery itself is hardware-gated and has no unit
test. The advertisement filter depends on the trainer's
advertisement carrying the FTMS service UUID — verify on the
Zwift Hub during hardware validation.

## 7. Validation split

Headless (this item, no radio):

- FTMS suite: parse flags → struct fields (including the 24-bit
  total-distance skip — fields after it must parse correctly),
  feature/target parsing → `supports_*` flags, power and
  resistance capping (increment rounding, min/max clamping),
  exact command bytes for target power, sim params
  (grade/wind/crr/cw), resistance, spin-down.
- Adapter suite (stub backend): connect/start/stop gating,
  command translation (ERG → target power, slope/sim →
  sim params, resistance 0–100 → 0.0–1.0), sample mapping,
  distance accumulation, disconnect → `trainer_connected`
  false and no emission, calibration state exposure,
  request/reset, no `QMessageBox`/toast leakage (the adapter
  never connects `setNotification`), queue-and-drain ordering
  (samples emitted only from `tick()`, on the calling thread).
- Regression: all suites pass, GUI build and `--version`
  unchanged, `check-core-includes` still passes (adapter and
  scanner live in `src/Train`, not core).

Hardware-gated (not this item, needs a Bluetooth-capable host
+ the Zwift Hub):

- BLE discovery/pairing, GATT service discovery, notification
  flow at ~1 Hz, ERG convergence on a real load unit,
  sim-gradient response, spin-down calibration end to end,
  disconnect/reconnect behaviour, multi-hour soak.
- Hardware regression comparison (the TrainSidebar-switch gate):
  same workout on the legacy GUI path vs the new core path,
  comparing recorded watts/speed/cadence traces.
- Watch items: the crr/cw truncation quirk (§3), the 4 Hz
  control-point write rate (§4), the FTMS service UUID in the
  Hub's advertisement (§6), frozen-trainer masking (§4).

## 8. Out of scope

GUI switch (TrainSidebar), ANT, a second driver, a native
rewrite, push-from-thread + mutex in `TrainSession`, the game
app (step 5, still unauthorised), FIT recording.
