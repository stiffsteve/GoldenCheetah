# Real trainer: hardware validation plan

Branch `core-view-split-phase0`. The headless adapter part is
done (fork tip `c095c8d7f`): `Bt40DeviceAdapter` over the BT40
stack, `Bt40Backend` + `Bt40Scanner`, core `ICalibration`,
FTMS protocol suite, adapter/scanner suites. This doc is the
hardware part: what to run against the real trainer, what
passes, and how the regression comparison against the legacy
GUI path works. The harness is `src/Train/
gc-train-ble-headless.cpp` (+ `.pro`); it drives the new core
path without the GUI switch, which stays gated.

## 0. Setup and assumptions

- **Hardware:** the user's Zwift Hub (FTMS over Bluetooth).
  It already works well with stock GoldenCheetah, so the
  legacy BT40 path is the proven baseline.
- **Host:** the user's Linux machine, where stock
  GoldenCheetah runs today. The machine's distro and Qt
  install are **not known** — the build steps below assume
  the repo's scripts (verified on Ubuntu 24.04) and say what
  to do when that does not apply. Do not guess the distro.
- **This VM cannot run any of this:** it has no Bluetooth
  hardware (`/sys/class/bluetooth` absent). The harness
  builds here and its logic is tested here with `--fake`;
  every radio step below runs only on the user's machine.
- **The rider pedals.** A trainer cannot generate watts by
  itself: in ERG mode it sets resistance so the rider's power
  equals the target; in sim mode the resistance follows the
  gradient and the rider supplies the watts. Every test below
  assumes the rider pedals at a steady cadence (~80–90 rpm)
  for the whole run. If the rider stops, watts fall to 0 and
  the run fails on samples/convergence — that is a rider
  stop, not a trainer fault; re-run it.
- **The Hub sleeps.** Pedal once to wake it before scanning;
  a sleeping Hub does not advertise.
- **Run from a terminal in the desktop session** where stock
  GoldenCheetah works. That session is the proven
  environment for Bluetooth access (BlueZ + D-Bus). If
  `bluetoothctl` does not work in that terminal, the harness
  will not either — fix Bluetooth first, not the harness.

## 1. The harness

`gc-train-ble-headless` is a console program (QCoreApplication
+ event loop, no window, no dialogs, no toasts). It goes
through the **real** path: `discoverAndRegisterBt40Devices`
(blocking scan) → `DeviceManager::scan()` → `DeviceManager::
open(id)` → `TrainSession` owns the adapter → scripted
workout → CSV. It ticks the adapter then the session every
250 ms of real time (queue-and-drain, as designed) and pumps
the event loop between ticks so BLE notifications arrive.

```
gc-train-ble-headless --scan [timeout_ms]
    Scan only: print discovered FTMS trainers as
    ble:<address> + name, one per line. Exit 0 if at least
    one found, 2 if none.

gc-train-ble-headless --device <id> [--workout file.erg]
    [--ftp 250] [--weight 75] [--duration S] [--out run.csv]
    [--sim-test] [--calibrate]
    Full run (default workout: 60 s at 150 W, then 60 s at
    250 W). <id> is ble:<address>, a raw address, or a name
    substring; omitted, the only trainer found is used, and
    several found with no --device is an error.

gc-train-ble-headless --fake [--out fake.csv] [same flags]
    Same run with a simulating fake backend (ERG convergence
    toward the target, speed from the core physics model, no
    radio, no scan). Tests the harness logic headlessly.
```

CSV columns (1 Hz, to `--out`, or stdout if omitted):
`t_s,watts,target_watts,cadence,speed_kph,distance_m,state,
power_valid,hr_valid,trainer_connected`. `t_s` is seconds
since session start. State changes, step changes, and errors
go to stderr as terse lines (`state=...`, `step=...`,
`ERROR: ...`). Exit codes: 0 pass; 1 usage error; 2 device/
scan/connect/no-samples/disconnect-unrecoverable; 3 check
failure (ERG convergence, calibration, sim assertion).

## 2. Test list

Each test is one harness run (or one `--scan`), judged by
its stated criterion. The rider pedals throughout (§0).

### (a) Discovery — scan finds the Hub as `ble:<address>`

- Run: `gc-train-ble-headless --scan 10000`.
- Pass: the Hub's address appears, with a name containing
  "Zwift" (case-insensitive), and the line's id is
  `ble:<address>`. The address match is authoritative; the
  name is a sanity check.
- Fail: no trainer listed, or the Hub listed without its
  address. If the Hub is visible in `bluetoothctl scan` but
  not in the harness scan, the FTMS-advertisement filter
  (`Bt40Scanner::isFtmsTrainer` — service UUID 0x1826 in
  the advertisement) is wrong for the Hub; that is a
  finding, not a retry — record the `bluetoothctl` output
  and stop.

### (b) Pairing / GATT service discovery

- Run: the full run below; this test is judged from its
  log, not a separate command.
- Pass: `connect()` returns true (a local BT adapter is
  present), the link comes up (`isConnected()` true) within
  30 s, and the first power-valid sample arrives within
  10 s of session start. No error response to a control-point
  write in the log.
- Fail: no local adapter, link never comes up, or no samples
  in 10 s. Check the Hub is awake and in range first;
  if it is, the failure is in the connect/GATT path.

### (c) Notification flow at ~1 Hz, with sane fields

- Run: the full ERG run; judged from the CSV.
- Pass: in every 10 s window while Running, at least 5
  power-valid samples (no gap over 3 s — the session's
  stale timeout); every sample's fields sane: watts 0–2000,
  cadence 0–200, speed 0–100 kph, heartrate 0 or 30–220.
- Fail: a gap over 3 s, or an out-of-range field. A single
  gap is a dropout to note; repeated gaps are a fail.

### (d) ERG convergence on the real load unit

- Run: the default workout (60 s at 150 W, 60 s at 250 W).
- Pass (checked by the harness, exit 3 otherwise): for each
  step, the mean watts over the settled window — the last
  45 s of the 60 s step, i.e. excluding the first 15 s after
  the step change — is within **max(±10 W, ±5%)** of the
  target, and no settled sample is more than ±25 W from the
  target.
- Why this band: the Hub's power accuracy is ±2%; the ERG
  loop needs a few seconds to converge a flywheel, and the
  rider's own power wobbles. ±5% / ±10 W is generous enough
  to tolerate real-trainer noise and a human rider, and
  tight enough to catch a broken command path — if the
  target never reached the trainer, watts would sit at the
  previous level, ~100 W off, far outside the band. The
  15 s settle window is the same reasoning: convergence
  takes seconds, not tens of seconds; 15 s is generous.
- Fail: a step's settled mean outside the band, or a
  settled sample beyond ±25 W. Note whether watts sit at
  the *previous* step's level (command not applied) or
  oscillate (loop instability) — different faults.

### (e) Sim-gradient response (0% then 8%)

- Run: the ERG run with `--sim-test` (30 s at 0%, then
  30 s at 8%, rider holding roughly steady watts).
- Pass: mean speed in the 8% phase is at least 15% lower
  than in the 0% phase, when the mean watts in the two
  phases are within 20% of each other. (At 200 W the
  physics gives ~29.6 kph flat vs ~10.2 kph at 8%; 15% is
  a deliberately low bar — it only has to show the gradient
  reached the trainer.)
- Inconclusive, not a fail: rider watts differed by more
  than 20% between the phases — the harness prints that
  and exits 0; judge from the CSV by eye.
- Fail: watts comparable but speed did not drop — the
  gradient command did not reach the trainer.

### (f) Spin-down calibration end to end, via ICalibration

- Run: the ERG run with `--calibrate` (runs after the
  workout). The harness prints prompts to stderr; follow
  them: pedal up to the target speed it prints, then stop
  pedalling and coast until it reports the result.
- Pass: calibration state reaches SUCCESS (0x07) within
  120 s of the request, with a spindown time > 0.
- Fail: FAILURE (0x08, or too-slow/too-fast variants) or
  the 120 s timeout. Record the state trace from the log.

### (g) Disconnect / reconnect behaviour

- Run: during an ERG run, turn the Hub off (or walk it
  out of range) for ~10 s, then turn it back on / bring
  it back. Deliberate, once.
- Pass: the log shows the session going PausedWithError
  within 3 s of the link dropping (the stale timeout);
  on the link returning, the harness resumes (connect +
  start) and the run continues in the same CSV, with no
  second file and no lost recorder state. The run may
  still fail on (d) for the interrupted step — judge (d)
  from the uninterrupted steps; this test is about the
  state machine, not the watts.
- Fail: no PausedWithError within 3 s, no resume within
  60 s of the link returning, or a crash/double file.

### (h) Soak — 30 min

- Run: a 30-minute ERG workout file (`--workout`, e.g.
  a steady 180 W file, or steps) with `--duration 1800`.
- Pass: no sample gap over 3 s, no disconnect, no
  PausedWithError, and every step's settled mean stays
  inside the (d) band for the whole run (no drift).
- Fail: any gap, disconnect, or a step drifting out of
  band late in the run. Record at what time it happened.

## 3. Regression comparison — legacy GUI vs new core path

This is the TrainSidebar-switch gate: the same workout on
the legacy GUI path vs the new core path (the harness),
comparing the recorded traces.

- **Same everything:** same workout file (the default
  60 s @150 + 60 s @250, or one `--workout` file used for
  both runs), same rider, same FTP/weight, same Hub, back
  to back with a short rest. Two runs: (1) stock
  GoldenCheetah GUI — ride the workout, export the recorded
  ride to CSV at 1 Hz (watts, cadence, speed); (2) the
  harness with the same file — `--out harness.csv`.
- **Alignment:** both runs start at workout start (t = 0).
  Align on the ERG step boundary: the target changes
  150 → 250 at t = 60 s in both. Compare per step, on the
  settled window (t = 15–60 s and t = 75–120 s). Do not
  compare the first 15 s of a step (convergence) and do not
  try to align sample-by-sample — the two recorders tick
  on different clocks.
- **Traces compared:** watts, speed, cadence, all at 1 Hz.
- **Tolerances (explicit):**
  - Per-step settled mean watts: the two runs within
    max(±10 W, ±5%) of each other.
  - Settle time — time from a step change to the first
    5 s window whose mean is inside the (d) band — within
    ±5 s between runs.
  - Mean speed per step within ±10% between runs.
  - No sample gap over 3 s in either run.
  - Cadence traces the same shape (both in the 70–100 rpm
    band the rider held); a dropout in one path and not
    the other is a finding.
- **Human-variability caveat, stated plainly:** the rider
  cannot reproduce their own power exactly, so the two
  watt traces will not be identical. The comparison is
  about *trainer behaviour* — convergence to the commanded
  target, settle time, no dropouts — not about identical
  watts. If the settled means match within the band and
  the settle times match, the trainer behaved the same
  under both paths, which is what the gate asks.
- **Pass:** all tolerances met. **Fail:** any tolerance
  missed — record which one, on which step, with the
  numbers from both CSVs.

## 4. What success looks like, and what stays gated

Success is: (a)–(h) green, and the regression comparison
green. That proves the adapter drives the real trainer as
well as the legacy path, and unblocks the *decision* on the
TrainSidebar switch.

Still gated and NOT started by this item: the TrainSidebar
switch itself (it needs this comparison green first), C ABI
scan/select, FIT conversion, the pilot plot/bus consumer
switch, the daemon (Q4), the game app (step 5).

## 5. Build and run on the user's Linux machine

The user pulls the branch and builds it — no prebuilt binary.

### 5.1 Pull the branch

```sh
git clone https://github.com/stiffsteve/GoldenCheetah.git
cd GoldenCheetah
git checkout core-view-split-phase0
```

(Or in an existing checkout: `git remote add fork
https://github.com/stiffsteve/GoldenCheetah.git`,
`git fetch fork`, `git checkout -b core-view-split-phase0
fork/core-view-split-phase0`. The branch lives on the fork;
upstream does not have it.)

### 5.2 Qt and system deps

**Assumed, not verified:** the machine is Ubuntu 24.04,
the only distro the repo's scripts cover. If it is:

```sh
./scripts/baseline-build.sh
```

That fixes the apt mirror, installs the system deps
(flex, bison, libgsl-dev, GL/EGL dev, libdbus-1-3,
libxtst6, … — full list in `scripts/README.md` §2),
installs Qt 6.8.3 via aqtinstall into `~/Qt/6.8.3/gcc_64`
(modules include qtconnectivity, which provides
QtBluetooth), and builds Qwt + `src`.

If the distro is **not** Ubuntu 24.04, `baseline-build.sh`
does not apply (it is apt-based): install Qt 6.8.3
yourself via aqtinstall with the same modules as
`scripts/README.md` §3 (qt5compat, qtcharts,
qtconnectivity, qtmultimedia, qtpositioning, qtserialport,
qtwebchannel, qtwebengine, qtimageformats, qtwebsockets),
install the equivalent system deps for your distro
(libgsl-dev, GL/EGL dev, libdbus-1-3, libxtst6, flex,
bison), then configure and build:

```sh
export PATH="$HOME/Qt/6.8.3/gcc_64/bin:$PATH"
cp qwt/qwtconfig.pri.in qwt/qwtconfig.pri
cp src/gcconfig.pri.in src/gcconfig.pri
cp unittests/unittests.pri.in unittests/unittests.pri
# edit src/gcconfig.pri per scripts/README.md §5 (GSL paths,
# flex/bison, LIBZ) if the defaults do not fit
qmake build.pro -r
make -j$(nproc) sub-qwt
make -j$(nproc) sub-src
```

The full `sub-src` build is required: the harness links
the objects it produces (see §5.3), and the legacy GUI
binary it produces is the other half of the regression
comparison (§3).

### 5.3 Build the harness

```sh
export PATH="$HOME/Qt/6.8.3/gcc_64/bin:$PATH"
cd src/Train
qmake gc-train-ble-headless.pro
make -j$(nproc)
```

This builds `gc-train-ble-headless` in `src/Train/`. It is
deliberately **not** wired into `build.pro`: a standalone
program, like the other headless programs. It links the
prebuilt objects from the `sub-src` build
(`Bt40DeviceAdapter`, `Bt40Backend`, `BT40Controller`,
`BT40Device`, `Ftms`, `Bt40Scanner`, `CalibrationData`,
`DeviceConfiguration`, `RealtimeData`, plus the BT40 moc
objects), so `sub-src` must have run first. Qt modules
needed: core + bluetooth only (`QProgressDialog` is an
unused include in `BT40Controller.cpp` — no widgets link).

### 5.4 Pre-flight checks

```sh
bluetoothctl show        # an adapter, powered on
bluetoothctl scan on     # the Hub appears (Ctrl-C to stop)
```

Pedal once to wake the Hub first. If `bluetoothctl` does
not work in this terminal, stop — the harness will not
work either.

### 5.5 Run it

```sh
cd src/Train
./gc-train-ble-headless --scan 10000
./gc-train-ble-headless --device ble:<address> --out run1.csv 2> run1.log
./gc-train-ble-headless --device ble:<address> --out run2.csv --sim-test 2> run2.log
./gc-train-ble-headless --device ble:<address> --out run3.csv --calibrate 2> run3.log
```

`--fake` runs the same logic with no radio (works
anywhere, including this VM):

```sh
./gc-train-ble-headless --fake --out fake.csv 2> fake.log
```

Keep the CSV and the stderr log for each run — they are
the evidence for §2 and §3. The harness prints `PASS` /
`FAIL` lines and exits non-zero on failure (§1).

## 6. Known watch items carried into hardware validation

From the adapter scope doc — check each on the Hub:

- The crr/cw truncation quirk: sim params compute
  `quint8 crr = rollingResistance` and `quint8 cw =
  windResistance` from doubles (defaults 0.0033 and 0.6),
  truncating to 0 — the trainer always receives crr = 0,
  cw = 0. Watch whether sim-mode resistance feels wrong;
  fixing it changes what the trainer receives.
- The 4 Hz control-point write rate (the session sends the
  command every tick, no dedupe) vs the legacy GUI's 1 Hz.
  Watch for the trainer missing or throttling writes.
- The FTMS service UUID in the Hub's advertisement — test
  (a) is the check.
- Frozen-trainer masking: a link that stays up while
  notifications stop keeps producing identical snapshots.
  Test (c)'s gap rule catches a full stop; a *frozen*
  (unchanging) watts value with the rider pedalling is the
  subtler form — watch the CSV for long runs of identical
  watts while cadence varies.
