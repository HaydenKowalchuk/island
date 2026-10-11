#!/usr/bin/env bash
# Package island as a double-clickable macOS .app bundle (island1-c990).
#
# This is intentionally a SEPARATE path from the regular dev build:
#   - It uses its own build dir (build-macos-app), never build/, so run.sh and
#     the normal `meson compile -C build` workflow are untouched.
#   - It assembles island.app/Contents/{MacOS,Resources} by hand from the
#     meson-built binary + the staged assets tree. No meson macos_bundle
#     magic, so nothing about the shared build files changes behavior.
#
# The binary finds bundled assets at runtime with NO env vars: bundle.c
# resolves ../Resources/assets relative to the executable, and the texture +
# audio loaders probe it as a fallback after ASSET_DIR/AUDIO_ASSET_DIR.
#
# GLFW + glad are linked statically (bundled wraps); the only dynamic deps are
# system frameworks, so there are no third-party dylibs to embed.
#
# Output: build-macos-app/dist/island.app  (ad-hoc signed, runs from Finder)
# Usage:  scripts/package_macos.sh
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$DIR/build-macos-app"
DIST="$BUILD/dist"
APP="$DIST/island.app"
CONTENTS="$APP/Contents"
BUNDLE_ID="com.ndc.island"
EXE_NAME="island"
# Bump only the min OS if we adopt newer APIs; GL 1.5 + GLFW run anywhere modern.
MIN_MACOS="11.0"

[ "$(uname -s)" = "Darwin" ] || { echo "package_macos: macOS only" >&2; exit 1; }

echo "==> configuring dedicated build dir (regular build/ untouched)"
mkdir -p "$BUILD"
meson setup "$BUILD" "$DIR" --buildtype debugoptimized >"$BUILD/setup.log" 2>&1 \
  || { echo "meson setup failed:" >&2; tail -20 "$BUILD/setup.log" >&2; exit 1; }

echo "==> building binary + staging assets"
meson compile -C "$BUILD" >"$BUILD/compile.log" 2>&1 \
  || { echo "meson compile failed:" >&2; tail -30 "$BUILD/compile.log" >&2; exit 1; }

BIN="$BUILD/src/$EXE_NAME"
ASSETS="$BUILD/src/assets"
[ -x "$BIN" ] || { echo "missing binary: $BIN" >&2; exit 1; }
[ -d "$ASSETS" ] || { echo "missing staged assets: $ASSETS" >&2; exit 1; }

echo "==> assembling $APP"
rm -rf "$APP"
mkdir -p "$CONTENTS/MacOS" "$CONTENTS/Resources"
cp "$BIN" "$CONTENTS/MacOS/$EXE_NAME"
# Whole staged tree (textures + audio); the loader probes Resources/assets.
cp -R "$ASSETS" "$CONTENTS/Resources/assets"

# --- Info.plist -------------------------------------------------------------
# Version comes from the project() call in meson.build so it stays in sync.
PROJECT_VERSION="$(sed -n "s/^[[:space:]]*version: '\([^']*\)'.*/\1/p" "$DIR/meson.build" | head -1)"
[ -n "$PROJECT_VERSION" ] || PROJECT_VERSION="0.0.0"

cat >"$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleDisplayName</key>
	<string>island</string>
	<key>CFBundleExecutable</key>
	<string>${EXE_NAME}</string>
	<key>CFBundleIdentifier</key>
	<string>${BUNDLE_ID}</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>island</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>${PROJECT_VERSION}</string>
	<key>CFBundleVersion</key>
	<string>${PROJECT_VERSION}</string>
	<key>LSMinimumSystemVersion</key>
	<string>${MIN_MACOS}</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>
PLIST

# --- Icon (best-effort; non-fatal) ------------------------------------------
# assets_psp/icon0.png is 144x80 (PSP aspect). Pad to square and upscale, then
# let iconutil build the .icns. If any tool is missing or it fails, ship
# without an icon -- macOS just shows the generic one.
# Primary source: packaging/macos/island_icon.png (square 1024x1024, cropped
# from a real attract-backdrop screenshot so the island + car read at small
# sizes). The PSP 144x80 icon0.png is the non-square fallback.
ICON_SRC="$DIR/packaging/macos/island_icon.png"
[ -f "$ICON_SRC" ] || ICON_SRC="$DIR/assets_psp/icon0.png"
if [ -f "$ICON_SRC" ] && command -v sips >/dev/null && command -v iconutil >/dev/null; then
  echo "==> generating .icns from $ICON_SRC"
  ICONSET="$BUILD/island.iconset"
  WORK="$BUILD/icon_work.png"
  rm -rf "$ICONSET"
  mkdir -p "$ICONSET"
  # Normalize to a square 1024 canvas (pad, never crop) so the non-square
  # PSP fallback won't distort; the primary source is already square.
  if sips -s format png "$ICON_SRC" --out "$WORK" >/dev/null 2>&1; then
    SQUARE="$BUILD/icon_square.png"
    sips -s format png --padToHeightWidth 1024 1024 "$WORK" --out "$SQUARE" >/dev/null 2>&1 || cp "$WORK" "$SQUARE"
    for sz in 16 32 64 128 256 512; do
      sips -z "$sz" "$sz" "$SQUARE" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null 2>&1 || true
      dbl=$((sz * 2))
      sips -z "$dbl" "$dbl" "$SQUARE" --out "$ICONSET/icon_${sz}x${sz}@2x.png" >/dev/null 2>&1 || true
    done
    if iconutil -c icns "$ICONSET" -o "$CONTENTS/Resources/${EXE_NAME}.icns" >/dev/null 2>&1; then
      /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string ${EXE_NAME}" "$CONTENTS/Info.plist" >/dev/null 2>&1 || true
    else
      echo "    iconutil failed; continuing without an icon"
      rm -f "$CONTENTS/Resources/${EXE_NAME}.icns"
    fi
    rm -rf "$ICONSET" "$WORK" "$SQUARE"
  else
    echo "    sips could not read $ICON_SRC; continuing without an icon"
  fi
else
  echo "==> skipping icon (source or tools unavailable)"
fi

# --- Ad-hoc sign ------------------------------------------------------------
# Ad-hoc signature so Gatekeeper lets it run locally. For distribution you'd
# swap in a Developer ID cert + notarization later.
echo "==> ad-hoc codesign"
codesign --force --sign - "$APP" >/dev/null 2>&1 \
  || echo "    codesign failed (non-fatal; may still run locally)"

echo ""
echo "done: $APP"
echo "  double-click in Finder, or: open \"$APP\""
echo "  (no env vars needed; assets resolve from Contents/Resources/assets)"
