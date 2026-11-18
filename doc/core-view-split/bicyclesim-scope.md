# BicycleSim core port — scope

Port the `BicycleSim` momentum model from `src/Train/` into `src/Core/`,
keeping the model intact and the GUI behaviour-neutral, then wire the
`VirtualRider` to it. Authorised 2026-11-18 ("Both, BicycleSim first").
Sequence: (a) port + pinning suite, then (b) wire VirtualRider. The game
app (step 5) is still NOT authorised.

## 1. What the model is

`src/Train/BicycleSim.h` / `src/Train/BicycleSim.cpp`, class `Bicycle`
plus helpers. A kinetic-energy model: power in, minus the power needed
to hold the current speed, becomes kinetic energy, which becomes speed.
Speed therefore carries momentum through watts and gradient changes
instead of jumping instantly.

- **State:** velocity `m_Velocity` (m/s) — the single dynamic state;
  previous tick state `m_state`; two `BicycleWheel`s; effective KE mass
  `m_KEMass`; constants; filter/hypoxia flags; wall-clock timer state.
- **Force model:** the same kreuzotter power-balance equation as
  `computeInstantSpeed` (`src/Core/PhysicsUtility.cpp`). `WattsForV`
  is the forward direction (power needed at speed v);
  `computeInstantSpeed` solves the cubic for v at given power.
- **Tick** (`SampleSpeed`, `BicycleSim.cpp:456`): filter watts,
  measure dt, integrate via `KahanLi8` (`Integrator.h`, 8th-order
  symplectic, 16 taps) with inputs linearly interpolated across the
  tick, step via energy (`V` / `KEFromV` / `VFromKE`).
- **Inputs per tick:** one `BicycleSimState` — watts, slope %, altitude
  m. No wind input: wind `W` is hardcoded 0 in both models.
- **Outputs:** `SpeedDistance { v, d }` — v in km/h, d in km this tick.
  Altitude is an input (the course supplies it), not an output;
  distance/altitude accumulation stays the caller's job.
- **Wheels:** each wheel has total mass, moment of inertia `m_I`, and
  equivalent mass `m_I / outerR²`. Default parts give 0.5325 kg per
  wheel, 1.065 kg total — about 1.3% on top of the 81.48 kg total
  (75 kg rider). KE mass 82.545 kg at 75 kg rider.
- **Watt filter** (`FilterWattIncrease`): a jump of more than 800 W
  above the previous tick clamps to previous + 200 W (anti-spike; it
  protects the integrator from eating free distance).
- **Altitude penalty** (`GetAltitudeAdjustmentFactor`): 3rd-order
  rational fit to the Townsend et al. hypoxia data (1.0 at sea level,
  ~0.515 at 7 km), normalised to the trainer's altitude. OUT for v1
  (decision 3 below).

## 2. The config: 28 values, not 29

The scoping report said 29; the code says 28 (`SimBicyclePage::
BicycleParts`, `src/Gui/Pages.h:256`, `LastPart` = 28). Defaults from
`SimBicyclePage::GetSimBicyclePartEntry`, `src/Gui/Pages.cpp:~890`:

| Value | Default | Value | Default |
|---|---|---|---|
| BicycleWithoutWheelsG | 4000 g | RearWheelG | 739 g |
| FrontWheelG | 739 g | RearSpokeCount | 24 |
| FrontSpokeCount | 24 | RearSpokeNippleG | 5.6 g |
| FrontSpokeNippleG | 5.6 g | RearRimG | 330 g |
| FrontRimG | 330 g | RearRotorG | 120 g |
| FrontRotorG | 120 g | RearSkewerG | 40 g |
| FrontSkewerG | 40 g | RearTireG | 220 g |
| FrontTireG | 220 g | RearTubeSealantG | 26 g |
| FrontOuterRadiusM | 0.35 m | RearOuterRadiusM | 0.35 m |
| FrontRimInnerRadiusM | 0.3 m | RearRimInnerRadiusM | 0.3 m |
| FrontTubeSealantG | 26 g | CassetteG | 190 g |
| CRR | 0.004 | Cd | 0.9955 |
| Cm | 1.0 | Am2 | 0.5 m² |
| Tk | 293.15 K | ActualTrainerAltitudeM | 0 m |

Derived totals at the defaults: front wheel 1145 g, rear wheel 1335 g
(cassette on rear), total mass 81.48 kg at 75 kg rider, KE mass
82.545 kg. Comment in the table: "Bike mass values approximate a
good current bike. Wheels are shimano c40 with conti tubeless tires."

## 3. Coupling table — what leaves, what replaces it

| Dependency | Where | Used for | Replaced by |
|---|---|---|---|
| `Context*` (ctor + `Reset`) | `BicycleSim.h`, `BicycleSim.cpp:203,210,216` | Athlete weight; passed to `SimBicyclePage::GetBicyclePartValue`. The 6-arg ctor takes `Context*` but `Q_UNUSED`s it. `Reset(NULL)` already tolerates NULL (80 kg rider, all defaults). | Plain `BicycleConfig` struct + `configure()`. A Train-side helper builds the config from a `Context*` for TrainSidebar (same values `Reset` read). |
| `RealtimeData` | `BicycleSimState(const RealtimeData&)` | Only three doubles: `getWatts()`, `getSlope()`, `getAltitude()`. | Plain `{watts, slope, altitude}` ctor, which already exists. The `RealtimeData` ctor is deleted; the two GUI call sites build the struct from three doubles. |
| `ErgFile.h` | `#include` in both files | Nothing. Dead include — delete, no behaviour impact. | — |
| `Settings.h` / `appsettings` | `Reset()` | Reads the 28 values (via `SimBicyclePage`) plus `TRAIN_USESIMULATEDHYPOXIA`. | Config struct. The GUI settings page is untouched; the Train-side helper translates settings → struct. |
| `SimBicyclePage` (`Pages.h/.cpp`, a `QWidget`) | `Reset()` | Only static `GetBicyclePartValue` and the defaults table. No widget behaviour. | Defaults table moves into the core config as default member values (§2). |
| `std::chrono::high_resolution_clock` | `SampleDT()` | Wall-clock dt. The one *behavioural* coupling: non-deterministic, untestable with a fake clock. | dt as a caller-provided parameter (decision 4). |
| `MultiRegressionizer.h` + `PolynomialRegression.h` | `GetAltitudeFit` | Hypoxia curve fit only. | NOT moved (hypoxia out for v1). The fit functions and the Townsend table are deleted from the ported `.cpp`. |
| `Integrator.h` | `SampleSpeed` | `KahanLi8` (+ 8 unused integrators). Pure std (`<tuple>` only). | Moved as-is to `src/Core`. Note: `PolynomialRegression.cpp:28` also includes it — its bare `#include "Integrator.h"` keeps resolving via the existing `./Core` include path, as with `PhysicsUtility` in step 2. |
| `PhysicsUtility.h` (`AirDensity`, `MsToKmh`, `s_g0`) | `WattsForV` | Already in core. No work. | — |
| Qt (any) | — | None in the physics. No `QObject`, `QString`, `QVector`, `QDate` (except via `Reset`'s `QDate::currentDate()`, which goes away with `Context*`). | — |

### Callers (only two, both in `src/Train/`)

- **`TrainSidebar`** (`TrainSidebar.h:56` include, `:330` member,
  constructed `bicycle(context)` at `TrainSidebar.cpp:83`):
  `Reset(context)` on config change (:805); `resettimer()` (:1302);
  `clear()` (:1391); `SampleSpeed` in the telemetry loop (:1967–1968)
  when `useSimulatedSpeed` is set; `MassKG()` feeds the
  `computeInstantSpeed` virtual-speed call (:2168);
  `RollingResistance()`/`MassKG()` pushed to controllers on start
  (:1760–1761); `WindResistance(displayAltitude)` on gradient change
  (:2370).
- **`NullController`** (`NullController.h:34` include, `:76` member,
  constructed `bicycle(NULL)` at `NullController.cpp:43`):
  `SampleSpeed` in `getRealtimeData` (:88–90) with randomised watts;
  `clear()` on start/restart (:53, :67). `NULL` context is the
  existing "default config" path.

`RealtimeController` does not reference `Bicycle`. No tests reference
it. The 6-arg explicit ctor (`Bicycle(Context*, constants, ...)`) is
used by no production caller — only the pre-port probe uses it.

## 4. Decisions (accepted 2026-11-18)

1. **Location/name.** `git mv` `src/Train/BicycleSim.h/.cpp` →
   `src/Core/BicycleSim.h/.cpp`, plus `src/Train/Integrator.h` →
   `src/Core/Integrator.h`. Keep the names `Bicycle`,
   `BicycleWheel`, `BicycleConstants`, `BicycleSimState`.
2. **Watt filter: IN.** ~6 lines, no dependencies; the game feeds
   keyboard-driven watts where spikes are plausible.
3. **Hypoxia: OUT for v1.** Keep the `m_AltitudeCorrectionFactor`
   field in `BicycleConstants` (default 1.0, inert) so it is a pure
   addition later; do NOT move `MultiRegressionizer.h` /
   `PolynomialRegression.h`. Game courses are near sea level and the
   GUI behaviour at default trainer altitude 0 is identical with or
   without it.
4. **dt: BOTH overloads.** `SampleSpeed(state)` preserves today's
   wall-clock behaviour (GUI call sites unchanged), plus
   `SampleSpeed(state, dt_seconds)` as the primary API for
   core/game/tests. Same rules as `SampleDT`: first tick dt =
   0.01 s, dt > 1.0 s clamped to 0.01 s, dt <= 0 means no change.
5. **Initial velocity: 0.** A ride starts from rest. Do not seed from
   `computeInstantSpeed` — that would hide the momentum.
6. **Config: FULL 28-value config.** `Reset(Context*)` becomes
   `configure(const BicycleConfig&)`; constructors collapse to
   `Bicycle(const BicycleConfig&)` and `Bicycle()` (all defaults +
   80 kg rider — the `bicycle(NULL)` path).
7. **Validation: TWO layers.** Port-correct = the core model
   reproduces the GUI model on identical inputs (exact for pure
   functions, ±0.5% steady state, ±2% integrated distance).
   Game-correct = steady state equals `computeInstantSpeed`,
   transients physical. Freeride proof bounds must be RE-MEASURED,
   not assumed — the old instant-model numbers are not the target.
8. **Sequencing.** (a) port + pinning suite (no GUI behaviour change,
   no `VirtualRider` change); (b) wire `VirtualRider` to the sim with
   re-measured freeride bounds.

## 5. The port, concretely

- **Files moved** (`git mv`): `BicycleSim.h/.cpp`,
  `Integrator.h` → `src/Core/`.
- **Files updated:** `src/src.pro` header list (:669) and source list
  (:788) — `Train/BicycleSim.*` → `Core/BicycleSim.*`
  (`Integrator.h` is not listed in `src.pro`, no change needed);
  `util/check-core-includes.sh` (add `BicycleSim.h`,
  `Integrator.h`); `godot/SConstruct` (link
  `../src/Core/BicycleSim.cpp`, as for `PhysicsUtility.cpp` /
  `BlinnSolver.cpp` in game step 3 — without it the `.so` fails to
  load); the two GUI includes (`TrainSidebar.h:56`,
  `NullController.h:34`) keep resolving bare via `./Core`.
- **API changes in the port:**
  - `BicycleSimState` loses the `RealtimeData` constructor; the two
    GUI call sites build it from three doubles.
  - `Reset(Context*)` becomes `configure(const BicycleConfig&)`;
    the Train-side helper `bicycleConfigFromContext(Context*)` (in
    TrainSidebar.cpp) builds the config the way `Reset` did, minus
    hypoxia. TrainSidebar constructs
    `bicycle(bicycleConfigFromContext(context))` and re-inits with
    `bicycle.configure(...)`; NullController constructs `Bicycle()`.
  - `SampleSpeed(state, dt_seconds)` added; `SampleDT` rules
    preserved in both overloads.
  - `ErgFile.h`, `Settings.h`, `Pages.h`, `Context` includes
    deleted; `GetAltitudeFit` / `GetAltitudeAdjustmentFactor` and
    the Townsend table deleted; `FilterWattIncrease` drops the
    altitude parameter and the hypoxia branch.
  - `Init` takes the config struct instead of the 6 separate
    arguments; the `m_useSimulatedHypoxia` member goes away with
    the hypoxia branch.
- **What stays in `Train/`:** nothing of the model. The GUI keeps
  calling the same methods on the same type, now from core.

## 6. Validation plan

### Probe first (before the port)

A probe program against the GUI model (explicit ctor, `NULL`
context — it `Q_UNUSED`s it — explicit constants, no athlete, no
settings, no Qt event loop). Wall-clock dt via real sleeps,
100 ms/tick. Cases, run in parallel:

1. **Pure-function pins (exact, ≤1e-9 relative):**
   `KEFromV`/`VFromKE` round-trip; wheel equivalent mass
   (default wheels → 0.5325 kg each; KE mass 82.545 kg at 75 kg
   rider); `WattsForV(v) == watts` at `v = computeInstantSpeed(...)`
   over a grid of (watts, slope, altitude) — compared at the SIM's
   mass and Cd (81.48 kg from the parts table, Cd 0.9955), not the
   83 kg / Cd 1.0 of the instant-model pins: the force model is
   shared, but `Frg` scales with mass, so comparing at 83 kg
   shows false diffs of up to 10 W on descents. Verified by the
   probe: all 60 grid points agree to 0.000000000 W.
2. **Steady state, 60 s from rest:** 250 W flat ≈ 32.15 kph,
   150 W flat ≈ 26.60 kph, 250 W at 8% ≈ 12.68 kph, 0 W flat → 0
   (the `computeInstantSpeed` values at the sim's 81.48 kg and
   Cd 0.9955 — NOT the 32.08 / 26.54 / 12.48 of the 83 kg pins).
3. **Step response:** from rest, 250 W flat — speed at 5/10/20/30/
   60 s and distance at 60 s; gradient step (20 s flat, then 8% at
   same watts) — speed at +5/+10/+20 s; watts step to 0 at speed —
   coast-down speed at +5/+10 s.
4. **Spike filter:** a single 1500 W sample after a 200 W tick must
   clamp to prev + 200 = **400 W** (not 300 — the clamp adds 200 to
   the previous tick's watts) — verified by running the 200/1500/
   200/200 sequence against a 200/400/200/200 sequence and comparing
   speeds. In the GUI model the two agree only to ~0.06%, because
   the hypoxia factor (~1.0009 at sea level) applies to the stored
   previous watts and again after the clamp; in the ported model
   (hypoxia removed) they are exactly equal, and the suite asserts
   exact equality.

Altitude-factor measurement is skipped (hypoxia out for v1).

### Probe results (measured 2026-11-18, before the port)

Probe program (throwaway, `/tmp/bicyclesim-probe.cpp`, not in the
repo): explicit ctor, NULL context, default parts, 75 kg rider,
100 ms/tick wall-clock dt. Mass 81.480 kg, KE mass 82.545012 kg,
wheel equivalent mass 0.532506 kg each, front 1.145 kg, rear
1.335 kg. `KEFromV`/`VFromKE` round-trip exact at 0/1/5/10/15 m/s.
`WattsForV` grid (60 points, watts 100–300, slopes 0/5/8/−5%,
altitudes 0/1000/2000 m): agrees with the watts input to
0.000000000 W at the sim's mass/Cd.

Steady state from rest (speed kph, distance m):

| Case | 5 s | 10 s | 20 s | 30 s | 60 s | dist @60 s |
|---|---|---|---|---|---|---|
| 250 W flat | 17.261 | 23.688 | 29.265 | 31.161 | 32.121 | 469.378 |
| 150 W flat | 13.373 | 18.521 | 23.382 | 25.299 | 26.523 | 379.881 |
| 250 W @8% | 11.062 | 12.326 | 12.670 | 12.690 | 12.691 | 203.692 |
| 0 W flat | 0 | 0 | — | — | 0 | 0 |

Oracle (steady state, sim mass/Cd): 250 W flat 32.152, 150 W flat
26.603, 250 W @8% 12.680 — the 60 s values sit 0.1–0.3% below the
oracle (still converging on the flat; the climb has converged).

Gradient step (20 s flat, then 8%, 250 W): 29.263 kph at the step,
19.468 at +5 s, 14.842 at +10 s, 12.833 at +20 s, 12.691 at 60 s;
distance 283.709 m at 60 s.

Coast-down (30 s at 250 W flat, then 0 W): 31.160 kph at the step,
26.092 at +5 s, 22.222 at +10 s, 16.699 at +20 s; distance
330.214 m at 50 s.

Spike filter (100 ms ticks): A (200/1500/200/200) gives
0.054 / 1.116 / 2.053 / 2.712 kph; C (200/400/200/200) gives
0.054 / 1.115 / 2.052 / 2.711 kph; B (200/300/200/200) gives
0.054 / 1.024 / 1.882 / 2.556 kph. A ≈ C (filter clamps to 400 W);
B is clearly different.

### Pinning suite

New suite `unittests/Core/bicycleSim/` (following the
`physicsUtility`/`virtualRider` pattern; `GC_OBJS = BicycleSim
PhysicsUtility BlinnSolver`). Drives the PORTED model with the
dt-explicit overload and fixed 100 ms steps — deterministic, no
sleeps (TDD 15.4). Tests: pure-function pins from (1); steady-state
convergence from (2) (±0.5%); step responses from (3) (±2%
distance, ±0.5 kph speed at sampled times); filter clamp from (4)
(exact — the two sequences must produce identical speeds).
Expected values are the probe numbers; if any expected value moves
during the port, the port is wrong, not the expectation.

### Freeride proofs (step b)

The existing `freeride` / `freeride-profile` scenes pin
instant-model numbers (flat 29.6 / climb 10.2 kph at 200 W;
profile 29.1 / 7.8). After the `VirtualRider` switch the
steady-state numbers are unchanged, but speeds at 15 s (flat) and
35 s (climb, 15 s after the gradient step) will be mid-transition:
flat a little below 29.6, climb a little above 10.2, distance a
little below 220 m. Re-measure from the new traces and re-derive
the bounds; do not tighten bounds to the old numbers.

## 7. Wiring VirtualRider (step b)

In `VirtualRider::tick()`, build
`BicycleSimState{current_watts_, gradient, altitude_m_}` and call
`bicycle_.sampleSpeed(state, dt_s)` instead of `computeInstantSpeed`;
distance from the sim's returned `d` (km → m); altitude still
accumulates in the device from gradient × distance. Everything else
in the device untouched; `start()` also calls `bicycle_.clear()`.
The device builds its `BicycleConfig` from `VirtualRiderConfig`:
rider weight from the config, bike mass without wheels =
`bike_weight_kg` − the table's wheel masses (front 1145 g + rear
1335 g = 2.48 kg), so total mass stays rider + bike exactly as the
instant model used it; crr/Cd/A/T forwarded from the device config.
`computeInstantSpeed` stays as the steady-state oracle.

## 8. Out of scope

The game app (step 5, still unauthorised), real trainers, FIT
recording, wind simulation (`W` is hardcoded 0 in both models — a
real wind input is a separate feature touching the course model,
not the sim), and any change to the GUI's use of the model beyond
the config-builder move in §5.
