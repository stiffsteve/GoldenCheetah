#!/usr/bin/env bash
#
# GoldenCheetah v3.8 — reproducible baseline build
# ------------------------------------------------
# Saved 2026-10-30, after the first successful qmake + Qwt build on this VM.
#
# WHY THIS EXISTS
#   The home directory (~/, including ~/Qt and ~/workspace) survives a VM
#   refresh/replacement, but anything outside it does NOT: /etc/apt sources,
#   /usr packages (flex, bison, GSL, GL, Qt runtime libs), etc.
#   So after a refresh, re-running this script should restore the system
#   side of the build. Qt itself lives in ~/Qt and usually survives, but
#   this script re-installs it if qmake is missing.
#
# DO NOT run this while another GoldenCheetah build is already running
# in ~/workspace/GoldenCheetah (it shares the same in-source build files).
#
# Usage:
#   ./goldencheetah-baseline-build.sh            # configure + build qwt + src
#   ./goldencheetah-baseline-build.sh --clean    # also remove ignored build
#                                                # artefacts first (git clean -fdX)
#   ./goldencheetah-baseline-build.sh --deps-only
#                                                # only fix apt + install system deps
#
set -euo pipefail

REPO="$HOME/workspace/GoldenCheetah"
QT_BIN="$HOME/Qt/6.8.3/gcc_64/bin"
QT_VERSION="6.8.3"
TAG="v3.8"
EXPECTED_COMMIT="409a550d2bca4e553535893aae65494da259fdb0"

CLEAN=false
DEPS_ONLY=false
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=true ;;
    --deps-only) DEPS_ONLY=true ;;
    *) echo "Unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# ---------------------------------------------------------------- apt deps
# The VM's original apt mirror (azure.archive.ubuntu.com + mirror.cogentco.com)
# was extremely slow (a 250 kB InRelease took ~13 s; apt-get update never
# finished in 5 minutes). archive.ubuntu.com fetched 35.3 MB in 34 s.
# The original file is backed up to ubuntu.sources.bak-azure-cogent.
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
  if grep -qE 'azure\.archive\.ubuntu\.com|mirror\.cogentco\.com' \
      /etc/apt/sources.list.d/ubuntu.sources; then
    cp /etc/apt/sources.list.d/ubuntu.sources \
       /etc/apt/sources.list.d/ubuntu.sources.bak-azure-cogent
    sed -i 's|URIs: .*|URIs: http://archive.ubuntu.com/ubuntu|' \
      /etc/apt/sources.list.d/ubuntu.sources
  fi
fi

export DEBIAN_FRONTEND=noninteractive
# 2026-10-30 VM refresh note: after a refresh, /usr dev packages are
# gone (home survives, /usr does not), and apt's configured proxy
# (hatch-egress-proxy:3128, via /etc/apt/apt.conf.d/80hatch-egress-proxy)
# failed with "Connection failed" to its IPv4 address, while the same
# host's IPv6 address worked (fetched 35.3 MB). So: try the normal
# update first, and on failure retry update + install with the proxy
# forced to the host's IPv6 address, resolved dynamically (do not
# hardcode the address — it may change on the next refresh).
APT_PROXY_OPTS=()
# 2026-11-01 correction: apt-get update returns 0 even when the
# Ubuntu indexes fail to fetch (it fetched cli.github.com fine),
# so "if ! apt-get update" never triggered the IPv6 retry, and
# the install then failed with "Unable to locate package" for
# every Ubuntu package. Detect the failure by checking the
# update output for archive failures AND by checking that flex
# actually has a candidate afterwards.
apt-get update 2>&1 | tee /tmp/gc-apt-update.log
if grep -q 'Failed to fetch http://archive\|Failed to fetch http://azure' /tmp/gc-apt-update.log \
   || ! apt-cache policy flex 2>/dev/null | grep -q 'Candidate: [0-9]'; then
  PROXY6="$(getent hosts hatch-egress-proxy | awk '$1 ~ /:/ {print $1; exit}')"
  if [ -n "$PROXY6" ]; then
    echo "apt update did not fetch Ubuntu indexes; retrying via hatch-egress-proxy IPv6 [$PROXY6]:3128"
    APT_PROXY_OPTS=(-o "Acquire::http::Proxy=http://[$PROXY6]:3128"
                    -o "Acquire::https::Proxy=http://[$PROXY6]:3128")
    apt-get "${APT_PROXY_OPTS[@]}" update
  else
    echo "apt update failed and no IPv6 address for hatch-egress-proxy; aborting" >&2
    exit 1
  fi
fi
# 2026-11-01 fallback, proven when both proxies were down: if
# apt still cannot locate packages, install the dev packages
# directly from /var/cache/apt/archives (which survives VM
# refreshes) with dpkg -i, bypassing apt lists and the proxy
# entirely. This is a last resort, not the normal path.
# 2026-11-17 correction: this fallback must not kill the script
# (set -e) when the cache is empty — the system replay's
# apt-get autoclean can empty /var/cache/apt/archives after a
# refresh, so the globs match nothing and dpkg -i errors out.
# A failed fallback just falls through to the apt-get install
# below, which works once the indexes are fetched.
if ! apt-cache policy flex 2>/dev/null | grep -q 'Candidate: [0-9]'; then
  echo "apt still has no Ubuntu candidates; installing dev packages from apt cache with dpkg -i"
  dpkg -i /var/cache/apt/archives/flex_*.deb /var/cache/apt/archives/bison_*.deb \
    /var/cache/apt/archives/libgsl27_*.deb /var/cache/apt/archives/libgslcblas0_*.deb \
    /var/cache/apt/archives/libgsl-dev_*.deb /var/cache/apt/archives/libglvnd-core-dev_*.deb \
    /var/cache/apt/archives/libglvnd-dev_*.deb /var/cache/apt/archives/libgl-dev_*.deb \
    /var/cache/apt/archives/libglx-dev_*.deb /var/cache/apt/archives/libopengl-dev_*.deb \
    /var/cache/apt/archives/libegl-dev_*.deb /var/cache/apt/archives/libgl1-mesa-dev_*.deb \
    /var/cache/apt/archives/libegl1-mesa-dev_*.deb /var/cache/apt/archives/libglu1-mesa-dev_*.deb \
    /var/cache/apt/archives/libgles-dev_*.deb /var/cache/apt/archives/libxtst6_*.deb \
    || true
fi
apt-get "${APT_PROXY_OPTS[@]}" install -y --no-install-recommends \
  flex bison libgsl-dev libglu1-mesa-dev libgl1-mesa-dev libegl1-mesa-dev \
  libfontconfig1 libfreetype6 libxkbcommon0 libxkbcommon-x11-0 libdbus-1-3 \
  libglib2.0-0 libxcb1 libxcb-xinerama0 libxcb-icccm4 libxcb-image0 \
  libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 \
  libxcb-sync1 libxcb-xfixes0 libxcb-xkb1 libxi6 libxrender1 libsm6 libice6 \
  libxext6 libx11-xcb1 libxcb-cursor0 libxtst6

# Versions actually installed on 2026-10-30 (Ubuntu 24.04 noble):
#   flex 2.6.4-8.2build1, bison 2:3.8.2+dfsg-1ubuntu0.24.04.1,
#   libgsl-dev 2.7.1+dfsg-6ubuntu2, libglu1-mesa-dev 9.0.2-1.1build1

if [ "$DEPS_ONLY" = true ]; then
  echo "System dependencies installed. Qt / repo steps skipped (--deps-only)."
  exit 0
fi

# ---------------------------------------------------------------- Qt (home)
# Standing rule: ALWAYS use the home-directory Qt, never system/apt Qt.
# Qt 6.8.3 is the version documented in GoldenCheetah's INSTALL-LINUX for
# this release. Latest available via aqtinstall on 2026-10-30 was 6.12.0,
# but it was deliberately NOT used: src.pro only requires Qt >= 6.5.3,
# and 6.8.3 is the known-good baseline for v3.8. A later Qt upgrade, if
# wanted, should be installed side-by-side in ~/Qt, not over 6.8.3.
if [ ! -x "$QT_BIN/qmake" ]; then
  python3 -m pip install --user --break-system-packages aqtinstall
  "$HOME/.local/bin/aqt" install-qt linux desktop "$QT_VERSION" linux_gcc_64 \
    -O "$HOME/Qt" \
    -m qt5compat qtcharts qtconnectivity qtmultimedia qtpositioning \
       qtserialport qtwebchannel qtwebengine qtimageformats qtwebsockets
fi
export PATH="$QT_BIN:$PATH"
export LC_ALL=C.UTF-8 LANG=C.UTF-8
qmake --version   # must say: Qt version 6.8.3 in $HOME/Qt/6.8.3/gcc_64/lib

# ---------------------------------------------------------------- repo
if [ ! -d "$REPO/.git" ]; then
  git clone --branch "$TAG" --single-branch \
    https://github.com/GoldenCheetah/GoldenCheetah.git "$REPO"
fi
cd "$REPO"
git rev-parse HEAD   # expect $EXPECTED_COMMIT for the v3.8 tag

if [ "$CLEAN" = true ]; then
  make clean || true
  git clean -fdX
fi

# ---------------------------------------------------------------- config
# These .pri files are git-ignored (see .gitignore: *.pri), so copying the
# templates is build setup, not a source change. Only copy if missing, so
# an existing tuned config is never clobbered on a re-run.
[ -f qwt/qwtconfig.pri ]      || cp qwt/qwtconfig.pri.in qwt/qwtconfig.pri
[ -f src/gcconfig.pri ]       || cp src/gcconfig.pri.in src/gcconfig.pri
[ -f unittests/unittests.pri ] || cp unittests/unittests.pri.in unittests/unittests.pri

# Apply the Linux settings from INSTALL-LINUX / the 2026-10-30 run.
# Idempotent: already-uncommented lines are left alone.
python3 - <<'PY'
import pathlib
p = pathlib.Path("src/gcconfig.pri")
t = p.read_text()
for old, new in [
    ("#GSL_INCLUDES = /usr/include\n#GSL_LIBS = -L/usr/lib -lgsl -lgslcblas -lm",
     "GSL_INCLUDES = /usr/include\nGSL_LIBS = -L/usr/lib -lgsl -lgslcblas -lm"),
    ("#QMAKE_LEX  = flex\n#QMAKE_YACC = bison",
     "QMAKE_LEX  = flex\nQMAKE_YACC = bison"),
    ("#QMAKE_MOVE = cp", "QMAKE_MOVE = cp"),
    ("#LIBZ_INCLUDE =\n#LIBZ_LIBS    = -lz", "LIBZ_INCLUDE =\nLIBZ_LIBS    = -lz"),
]:
    t = t.replace(old, new)
p.write_text(t)
PY
grep -E '^(GSL_|QMAKE_LEX|QMAKE_YACC|QMAKE_MOVE|LIBZ_)' src/gcconfig.pri

# ---------------------------------------------------------------- build
# Upstream pattern (.github/scripts/build.sh, macOS CI): in-source qmake,
# then qwt first, then src. Build log goes outside the repo so it survives
# a repo clean.
qmake build.pro -r
make -j"$(nproc)" sub-qwt
ls -lh qwt/lib/libqwt.a
make -j"$(nproc)" sub-src > "$HOME/workspace/goldencheetah-baseline-build.log" 2>&1
echo "Main build finished. Binary (if linked): src/GoldenCheetah"
ls -lh src/GoldenCheetah || true

# Unit tests (opt-in suites under unittests/, enabled by unittests.pri):
#   make -j"$(nproc)" sub-unittests
# Then run each built test binary (QT_QPA_PLATFORM=offscreen) and record
# pass/fail against the baseline. Baseline 2026-10-30: all 8 suites pass;
# seasonParser must be run from its own directory so it finds seasons.xml.
# Also install libxtst6 (in the apt list above) or the linked binary will
# fail to start with libXtst.so.6 missing.
