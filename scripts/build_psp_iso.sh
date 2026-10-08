#!/usr/bin/env bash
# Package the PSP build into a bootable UMD ISO. Run after the cross build:
#
#   meson setup build_psp --buildtype=release -Dplatform=psp --cross-file mips-allegrex-psp
#   meson compile -C build_psp
#   ./scripts/build_psp_iso.sh          -> build_psp/island.iso
#
# Staging happens in .scratch/psp_iso; the base skeleton in tools/iso_root
# (copy of ~/Documents/Games/iso_root, example in tools/iso_root/build_iso.sh.example)
# is only ever read, never written. The XMB art lives in assets_psp/ as
# icon0.png (144x80) + pic1.png (480x272), like dc/platformer's assets_psp.
#
# Requires cdrtools: brew install cdrtools
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$DIR/build_psp"
BASE="$DIR/tools/iso_root"
ART="$DIR/assets_psp"
STAGE="$DIR/.scratch/psp_iso"
OUT="$BUILD/island.iso"
DISC_ID="HOME-90002"

die() { echo "build_psp_iso: $*" >&2; exit 1; }

command -v mkisofs >/dev/null 2>&1 || die "mkisofs not found (brew install cdrtools)"
[ -f "$BUILD/src/BOOT.BIN" ]    || die "missing $BUILD/src/BOOT.BIN — run: meson compile -C build_psp"
[ -f "$BUILD/src/param.sfo" ]   || die "missing $BUILD/src/param.sfo — run: meson compile -C build_psp"
[ -d "$BUILD/src/assets" ]      || die "missing $BUILD/src/assets — psp-copy-assets did not run"
[ -f "$BASE/PSP_GAME/PARAM.SFO" ] || die "missing base skeleton at $BASE"
[ -f "$ART/icon0.png" ]         || die "missing $ART/icon0.png"
[ -f "$ART/pic1.png" ]          || die "missing $ART/pic1.png"

# Fresh staging tree from the base skeleton (UMD layout).
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$BASE/." "$STAGE/"
rm -f "$STAGE/build_iso.sh.example"

# This game: title SFO, boot module, EBOOT.BIN is just a copy of BOOT.BIN,
# XMB art from assets_psp (uppercased names on the disc).
cp "$BUILD/src/param.sfo" "$STAGE/PSP_GAME/PARAM.SFO"
cp "$BUILD/src/BOOT.BIN"  "$STAGE/PSP_GAME/SYSDIR/BOOT.BIN"
cp "$BUILD/src/BOOT.BIN"  "$STAGE/PSP_GAME/SYSDIR/EBOOT.BIN"
cp "$ART/icon0.png"       "$STAGE/PSP_GAME/ICON0.PNG"
cp "$ART/pic1.png"        "$STAGE/PSP_GAME/PIC1.PNG"

# USRDIR holds only our data. The texture loader falls back to the relative
# root "assets", which resolves here when cwd is PSP_GAME/USRDIR on the disc.
#
# HARDWARE CONSTRAINT: every ISO must carry the retail XA 14-byte system-use
# field on all directory records (applied automatically below via iso_uxa.py).
# Firmware open() returns ENOENT for records without it when the name exceeds
# ISO9660 8.3, even though sceIoDopen lists the file. Proven A/B on hardware:
#   long name + no SU   (island_long)    -> ENOENT, texture missing
#   long name + SU14    (island_long_uxa)-> works perfectly
#   8.3 name  + no SU   (island_base)    -> works (SU not required <=8.3)
# mkisofs -xa/-XA alone is NOT equivalent: dirs match retail but file records
# differ (09 11 58 41 vs retail 0d 55 58 41). Keep running iso_uxa.py on
# every image; long filenames are fine once patched.
rm -rf "$STAGE/PSP_GAME/USRDIR"
mkdir -p "$STAGE/PSP_GAME/USRDIR"
cp -R "$BUILD/src/assets" "$STAGE/PSP_GAME/USRDIR/assets"

# UMD metadata: exactly "ID|1|1|" with no trailing newline.
printf '%s|1|1|' "$DISC_ID" > "$STAGE/UMD_DATA.BIN"

# macOS junk must not reach the image (xattrs make mkisofs unhappy).
xattr -cr "$STAGE" 2>/dev/null || true
find "$STAGE" -name '.DS_Store' -delete

mkisofs -A "PSP GAME" -V "NDC_ISLAND" -sysid "PSP GAME" -volset "" -p "" \
    -publisher "" -U -o "$OUT" "$STAGE"

# Mandatory: inject the retail XA system-use field (long-name open fix above).
python3 "$DIR/scripts/iso_uxa.py" "$OUT"

echo "build_psp_iso: wrote $OUT ($(stat -f%z "$OUT") bytes)"
