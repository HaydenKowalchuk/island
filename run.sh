#!/usr/bin/env bash
# Build and run island. Meson/ninja stdout goes to .scratch/build_log.txt; stderr
# stays on the console so warnings and errors are visible.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$DIR/build"
LOG="$DIR/.scratch/build_log.txt"
mkdir -p "$DIR/.scratch"

if [ ! -f "$BUILD/build.ninja" ]; then
    meson setup "$BUILD" "$DIR" > "$LOG"
fi

meson compile -C "$BUILD" > "$LOG"

export ASSET_DIR="$BUILD/src/assets"
exec "$BUILD/src/island" "$@"