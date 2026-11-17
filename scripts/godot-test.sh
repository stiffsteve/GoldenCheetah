#!/usr/bin/env bash
#
# Godot second client — headless automated test for GcTrainSession.
#
# Runs godot/test_gctrain_session.gd (extends SceneTree, no
# window/GPU/framework): the Phase 5 dashboard workout through
# the GDExtension node, asserting getters, step/state signals,
# manual commands, pause/resume, and live metrics against the
# same bounds as the C harness (src/Core/gc-session-c-test.c).
# Exit 0 and "gctrain-test: PASS" on success, 1 on any failure.
#
# Requires the plugin built and the project imported once
# (scripts/godot-setup.sh does both). The Godot binary is
# $HOME/godot/Godot_v4.5.1-stable_linux.x86_64 unless GODOT_BIN
# is set.
#
# Usage:
#   ./scripts/godot-test.sh

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GODOT_BIN="${GODOT_BIN:-$HOME/godot/Godot_v4.5.1-stable_linux.x86_64}"
PLUGIN_SO="$REPO/godot/bin/libgctrain.linux.template_debug.x86_64.so"

if [ ! -x "$GODOT_BIN" ]; then
    echo "Godot binary not found at $GODOT_BIN (run scripts/godot-setup.sh)" >&2
    exit 1
fi
if [ ! -f "$PLUGIN_SO" ]; then
    echo "Plugin not built at $PLUGIN_SO (run scripts/godot-setup.sh)" >&2
    exit 1
fi

"$GODOT_BIN" --headless --path "$REPO/godot" \
    -s "$REPO/godot/test_gctrain_session.gd"
