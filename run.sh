#!/usr/bin/env bash
# Build and run island. Meson/ninja stdout goes to build_log.txt; stderr stays on
# the console so warnings and errors are visible.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$DIR/build"
LOG="$DIR/build_log.txt"

if [ ! -f "$BUILD/build.ninja" ]; then
    meson setup "$BUILD" "$DIR" > "$LOG"
fi

meson compile -C "$BUILD" > "$LOG"

exec "$BUILD/src/island" "$@"