# Scripts — GoldenCheetah v3.8 baseline and Godot second client

This directory holds the setup and build scripts for the
core/view-split work on this branch (`core-view-split-phase0`,
from the v3.8 tag). They are developer tooling for this
branch, not upstream GoldenCheetah build files: upstream's
own build entry points are `build.pro` and
`.github/scripts/build.sh`.

## Contents

- `baseline-build.sh` — reproducible v3.8 baseline build on
  Ubuntu 24.04: fixes the apt mirror, installs the system
  dependencies, installs Qt 6.8.3 via aqtinstall if missing,
  configures and builds Qwt and `src`. Run it after a VM
  refresh (see below). Do not run it while a build is already
  running in the same checkout — it shares the in-source
  build files. Usage:
  ```sh
  ./scripts/baseline-build.sh            # configure + build qwt + src
  ./scripts/baseline-build.sh --clean    # also remove ignored build artefacts first
  ./scripts/baseline-build.sh --deps-only # only fix apt + install system deps
  ```
- `godot-setup.sh` — installs Godot 4.5.1 and godot-cpp
  4.5-stable, builds the GDExtension plugin in `godot/`,
  and runs the one-time editor import. Only needed for the
  Godot second client (see `doc/core-view-split/
  godot-second-client-plan.md`).
- `godot-test.sh` — runs the headless automated test for
  the GDExtension node (`godot/test_gctrain_session.gd`,
  23 checks, no window/GPU/framework). Requires the plugin
  built and the project imported once (`godot-setup.sh`
  does both). Exit 0 and "gctrain-test: PASS" on success.

## What survives a VM refresh, and what does not

- **Survives (home directory):** `$HOME/Qt/6.8.3/gcc_64`,
  `$HOME/.local/bin/aqt`, `$HOME/godot`, `$HOME/workspace/`
  including this checkout and its build artefacts.
- **Does NOT survive (outside home):** `/etc/apt` sources,
  and all packages installed under `/usr` — flex, bison,
  GSL, GL/EGL dev, Qt runtime libraries. Those are exactly
  what `baseline-build.sh` re-installs first. The apt cache
  in `/var/cache/apt/archives` does not reliably survive
  either (a system package replay can autoclean it), so the
  script's dpkg-cache fallback is a best effort only — the
  normal path is installing from the archive after the
  indexes are fetched.

## Baseline build, in order

### 1. Fix apt (the slow-mirror problem)
The VM's original apt source used `azure.archive.ubuntu.com`
and `mirror.cogentco.com`. A 250 kB `InRelease` took ~13 s
there, and `apt-get update` never finished in 5 minutes.
Switching to `archive.ubuntu.com` fetched 35.3 MB in 34 s.
The script re-does this step after a refresh, since the
original file is itself outside home.

### 2. Install system dependencies (Ubuntu 24.04 noble)
```sh
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  flex bison libgsl-dev libglu1-mesa-dev libgl1-mesa-dev libegl1-mesa-dev \
  libfontconfig1 libfreetype6 libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 \
  libglib2.0-0 libxcb1 libxcb-xinerama0 libxcb-icccm4 libxcb-image0 \
  libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
  libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxi6 libxrender1 libsm6 libice6 \
  libxext6 libx11-xcb1 libxcb-cursor0 libxtst6
```
`libxtst6` is a runtime requirement, not just a build one:
without it the linked binary fails to start with
`libXtst.so.6: cannot open shared object file` (it was also
the only linker warning during the baseline link).
Installed versions on 2026-10-30: flex 2.6.4-8.2build1,
bison 2:3.8.2+dfsg-1ubuntu0.24.04.1, libgsl-dev
2.7.1+dfsg-6ubuntu2, libglu1-mesa-dev 9.0.2-1.1build1.

The apt proxy can also bite after a refresh: the configured
proxy host (`hatch-egress-proxy:3128`) has failed on its IPv4
address while its IPv6 address worked. The script detects a
failed Ubuntu index fetch (note: `apt-get update` returns 0
even when indexes fail) and retries the update with the
proxy forced to its IPv6 address, resolved dynamically.

### 3. Qt — home directory only, never system/apt Qt
Qt 6.8.3, the version documented in `INSTALL-LINUX` for this
release (latest via aqtinstall at the time was 6.12.0,
deliberately not used for the v3.8 baseline; `src.pro`
requires only >= 6.5.3).

```sh
python3 -m pip install --user --break-system-packages aqtinstall
~/.local/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt \
  -m qt5compat qtcharts qtconnectivity qtmultimedia qtpositioning \
     qtserialport qtwebchannel qtwebengine qtimageformats qtwebsockets
export PATH="$HOME/Qt/6.8.3/gcc_64/bin:$PATH"
export LC_ALL=C.UTF-8 LANG=C.UTF-8
qmake --version  # Qt version 6.8.3 in $HOME/Qt/6.8.3/gcc_64/lib
```

Modules installed: qt5compat, qtcharts, qtconnectivity,
qtmultimedia, qtpositioning, qtserialport, qtwebchannel,
qtwebengine, qtimageformats, qtwebsockets. Base archives
also include qtbase, qtdeclarative, qtsvg, qttools
(lrelease), qttranslations, qtwayland. aqtinstall 3.3.0,
installed via pip --user.

### 4. Source checkout
```sh
git clone --branch v3.8 --single-branch \
  https://github.com/GoldenCheetah/GoldenCheetah.git ~/workspace/GoldenCheetah
cd ~/workspace/GoldenCheetah
git rev-parse HEAD  # 409a550d2bca4e553535893aae65494da259fdb0, detached HEAD
```

### 5. Build configuration (git-ignored, not source changes)
```sh
cp qwt/qwtconfig.pri.in qwt/qwtconfig.pri
cp src/gcconfig.pri.in src/gcconfig.pri
cp unittests/unittests.pri.in unittests/unittests.pri
```
Then in `src/gcconfig.pri`, uncomment/set:
```
GSL_INCLUDES = /usr/include
GSL_LIBS = -L/usr/lib -lgsl -lgslcblas -lm
QMAKE_LEX  = flex
QMAKE_YACC = bison
QMAKE_MOVE = cp        # bison 3.8 > 3.7, per the template
LIBZ_INCLUDE =
LIBZ_LIBS    = -lz
```
Video is off by default in the template (`DEFINES +=
GC_VIDEO_NONE`), and Python/R support is off — matching the
baseline run.

### 6. Configure and build (upstream in-source pattern)
```sh
qmake build.pro -r
make -j$(nproc) sub-qwt          # produces qwt/lib/libqwt.a
make -j$(nproc) sub-src > ~/workspace/goldencheetah-baseline-build.log 2>&1
```
Machine at the time: 2 CPUs, 7.7 GB RAM. Qwt took ~3 minutes;
the main build is much longer.

### 7. Unit tests — baseline result, 2026-10-30: all 8 pass
The 8 opt-in suites under `unittests/` (season, seasonOffset,
seasonParser, signalSafety, splineCrash, units, utils,
calendarData) are enabled by `unittests.pri`
(`GC_UNITTESTS = active`).

```sh
make -j$(nproc) sub-unittests
# run each binary with QT_QPA_PLATFORM=offscreen, from the repo root,
# EXCEPT seasonParser, which must be run from its own directory
# (unittests/Core/seasonParser) so it finds seasons.xml
```

Baseline: season 32 passed, seasonOffset 8 passed, seasonParser
3 passed (from its directory; 1 failure from the repo root is a
working-directory artefact, not a code failure), signalSafety
4+2 passed with 2 skipped (missing Python checker scripts),
splineCrash 3 passed, units 8 passed, utils 3 passed,
calendarData 3 passed.

Also observed, non-fatal, in `--version` output: a PipeWire
warning (`qt.multimedia.symbolsresolver: Couldn't load
pipewire-0.3`) and a `QObject::connect ... invalid nullptr
parameter` warning. Neither prevents the binary from running;
record them as pre-existing baseline observations, not as
refactor regressions.

**Status on 2026-10-30:** BASELINE COMPLETE. qmake succeeded,
Qwt built (`qwt/lib/libqwt.a`, 4.3 MB), main `sub-src` build
linked with exit 0 and 0 errors to `src/GoldenCheetah`
(27 MB). The binary runs offscreen and reports V3.8 (5015),
Qt 6.8.3, QWT 6.8.0, GCC 13.3.0, GSL 2.7.1. No source code
was modified; only git-ignored build config/files were
created.

## Known deviations from the TDD's description
- `src/` is not flat in v3.8; it is already split into ANT,
  Charts, Cloud, Core, FileIO, Gui, Metrics, Planning,
  Python, R, Train, Resources.
- There is no single `TrainView` file; train UI is spread
  across `TrainSidebar`, `TrainBottom`, `RealtimePlotWindow`,
  etc.
