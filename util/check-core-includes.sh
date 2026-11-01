#!/usr/bin/env bash
#
# check-core-includes.sh — Phase 0 core/view split build check
#
# Fails if a Phase 0 pure-type header includes anything from Qt or Qwt.
#
# Scope (deliberately narrow, see doc/core-view-split/phase0-inventory.md):
#   Only the Phase 0 headers are checked (seven types plus Mapping.h).
#   src/Core/ would fail on day one for pre-existing leakage
#   (GcCalendarModel, IntervalItem, NamedSearch, TimeUtils, and
#   SplineLookup via qwt_spline_*), and would block Phase 0. The check
#   is extended file-by-file as those headers are cleaned up.
#
# Strictness (deliberately stricter than the TDD's Q1 decision):
#   Q1 allows Qt Core in core and forbids Qt Widgets and Qwt. These
#   Phase 0 headers are pure standard C++ (only <cstdint>, <string>,
#   <vector>, <chrono>), so this script forbids *any* Qt or Qwt include
#   in them. If a future Phase 0 header legitimately needs Qt Core,
#   update this script's pattern list in the same commit, with a
#   comment explaining why — do not silently weaken it.
#
# Usage:
#   util/check-core-includes.sh            # check the Phase 0 headers
#   util/check-core-includes.sh FILE...    # check the given files instead
#   util/check-core-includes.sh --help
#
# Exit status: 0 if no forbidden include is found, 1 otherwise.
# This is a documented manual step for Phase 0, not wired into qmake
# or CI yet; wiring it in is a separate, later decision.
#
set -uo pipefail

usage() {
    sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

# Resolve the repo root from this script's location (util/ -> root),
# so the script works from any working directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ "$#" -gt 0 ]; then
    FILES=("$@")
else
    FILES=(
        "$REPO_ROOT/src/Core/TelemetrySample.h"
        "$REPO_ROOT/src/Core/DeviceInfo.h"
        "$REPO_ROOT/src/Core/SessionState.h"
        "$REPO_ROOT/src/Core/Workout.h"
        "$REPO_ROOT/src/Core/TrainerCommand.h"
        "$REPO_ROOT/src/Core/AthleteProfile.h"
        "$REPO_ROOT/src/Core/Clock.h"
        "$REPO_ROOT/src/Core/Mapping.h"
        "$REPO_ROOT/src/Core/WorkoutPlayer.h"
        "$REPO_ROOT/src/Core/WorkoutParser.h"
        "$REPO_ROOT/src/Core/TelemetryBus.h"
        "$REPO_ROOT/src/Core/TelemetryMerge.h"
        "$REPO_ROOT/src/Core/IDevice.h"
        "$REPO_ROOT/src/Core/FakeDevice.h"
        "$REPO_ROOT/src/Core/DeviceManager.h"
        "$REPO_ROOT/src/Core/LiveMetrics.h"
        "$REPO_ROOT/src/Core/Recorder.h"
        "$REPO_ROOT/src/Core/TrainSession.h"
    )
fi

# Forbidden include patterns (extended regular expressions, matched
# against lines starting with #include, allowing leading whitespace).
#   <Q...        any Qt module header (QtCore, QtGui, QtWidgets, ...)
#   "qwt...      Qwt headers included with quotes
#   <qwt...      Qwt headers included with angle brackets
#   "Qt...       Qt headers included with quotes (defensive; <Q covers most)
FORBIDDEN_PATTERN='^[[:space:]]*#[[:space:]]*include[[:space:]]*(<Q|"qwt|<qwt|"Qt)'

failures=0
for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "check-core-includes: missing file: $file" >&2
        failures=$((failures + 1))
        continue
    fi
    # grep -E exits 1 on no match; that is the passing case, not an error.
    if grep -q -E "$FORBIDDEN_PATTERN" "$file"; then
        echo "check-core-includes: FAIL: forbidden Qt/Qwt include in $file:" >&2
        grep -n -E "$FORBIDDEN_PATTERN" "$file" | sed 's/^/  /' >&2
        failures=$((failures + 1))
    fi
done

if [ "$failures" -gt 0 ]; then
    echo "check-core-includes: FAIL: $failures file(s) with forbidden includes" >&2
    exit 1
fi

echo "check-core-includes: PASS: ${#FILES[@]} file(s), no Qt/Qwt includes"
exit 0
