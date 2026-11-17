#!/usr/bin/env bash
#
# Godot second client — setup (core/view split, TDD 14.2).
#
# Recorded from the actual run on 2026-11-17, Ubuntu 24.04.
# Installs Godot 4.5.1 and godot-cpp 4.5-stable, builds the
# GDExtension plugin in godot/, and runs the one-time editor
# import that registers the extension with the project.
#
# Versions, and why these exact ones:
#   Godot 4.5.1-stable — latest release with a matching
#     godot-cpp tag. Godot 4.7.2 was the latest release, but
#     godot-cpp had no 4.7 tag at the time, so 4.5.1 was
#     chosen as the latest version with a matching godot-cpp.
#   godot-cpp godot-4.5-stable (commit e83fd09)
#   SCons 4.11.1, installed via pip --user (plain
#     `pip install scons` fails on Ubuntu 24.04, PEP 668:
#     externally managed environment).
#
# Idempotent: steps already done are skipped, so re-running
# after a VM refresh only restores what is missing. The
# godot-cpp build takes ~20 minutes on 4 cores the first
# time; later runs are incremental. The Godot binary lives
# in $HOME/godot (survives a VM refresh, like $HOME/Qt);
# godot-cpp is a build dependency inside godot/ and is
# git-ignored, not part of the repo.
#
# Usage:
#   ./scripts/godot-setup.sh
#
# After it succeeds, the headless proof run is:
#   godot --headless --path godot
# (use the binary at $HOME/godot/Godot_v4.5.1-stable_linux.x86_64,
# or put $HOME/godot on PATH first). It should exit 0 and print
# "godot-client: PASS" with final NP ~218.6, W'bal 20000, TSS 1.37.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GODOT_VERSION="4.5.1"
GODOT_BIN="$HOME/godot/Godot_v4.5.1-stable_linux.x86_64"
GODOT_ZIP="$HOME/godot/Godot_v4.5.1-stable_linux.x86_64.zip"
GODOT_URL="https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/Godot_v${GODOT_VERSION}-stable_linux.x86_64.zip"
GODOT_CPP_DIR="$REPO/godot/godot-cpp"

export PATH="$HOME/.local/bin:$PATH"

# 1. Godot editor binary.
if [ ! -x "$GODOT_BIN" ]; then
  mkdir -p "$HOME/godot"
  curl -L -o "$GODOT_ZIP" "$GODOT_URL"
  unzip -o "$GODOT_ZIP" -d "$HOME/godot"
  chmod +x "$GODOT_BIN"
fi
"$GODOT_BIN" --version  # must say: 4.5.1.stable.official

# 2. godot-cpp, matching branch.
if [ ! -d "$GODOT_CPP_DIR/.git" ]; then
  git clone --depth 1 --branch godot-4.5-stable \
    https://github.com/godotengine/godot-cpp.git "$GODOT_CPP_DIR"
fi
(cd "$GODOT_CPP_DIR" && git log --oneline -1)  # expect e83fd09 (4.5-stable)

# 3. SCons (pip --user; plain pip fails, PEP 668).
if ! command -v scons >/dev/null 2>&1; then
  python3 -m pip install --user --break-system-packages scons
fi
scons --version | head -1

# 4. Build the plugin (godot-cpp library first, then the
#    extension). Incremental on re-runs.
(cd "$REPO/godot" && scons platform=linux -j"$(nproc)")
ls -lh "$REPO/godot/bin/libgctrain.linux.template_debug.x86_64.so"

# 5. One-time editor import. Without this, the extension is
#    not in godot/.godot/extension_list.cfg and a plain run
#    loads a placeholder node instead of GcTrainSession.
#    (A first import once crashed with signal 11 while
#    main.gd still had a parse error; with a clean script it
#    exits 0.)
timeout 60 "$GODOT_BIN" --headless --editor --quit --path "$REPO/godot"
grep -q gctrain "$REPO/godot/.godot/extension_list.cfg"

echo "Godot setup complete. Headless proof run:"
echo "  $GODOT_BIN --headless --path $REPO/godot"
