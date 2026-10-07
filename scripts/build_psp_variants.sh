#!/usr/bin/env bash
# Build labelled ISO variants for hardware A/B tests of disc-open behaviour.
# Each variant carries a PARAM.SFO title naming it (visible on the XMB) and a
# matching volume id:
#
#   island_base.iso  current pack (-U, no system use)          HW: boots, assets load
#   island_rr.iso    + Rock Ridge (-r): SUSP system-use present HW: REFUSES TO BOOT
#                    (retail discs have no RR either — never ship -r images)
#   island_uxa.iso   + the exact 14-byte system-use field retail UMDs have
#                    (00 00 00 00 <8d|0d> 'UXA' + 6 NULs), patched into every
#                    directory record with scripts/iso_uxa.py
#                    HW: boots — SU tolerated but unnecessary; the real fix
#                    for the original ENOENT was parcel_32.png -> parcel.png
#                    (8.3 record names; mkisofs -U long records failed open
#                    on hardware even though dopen listed them).
#
# The canonical build_psp/island.iso is rebuilt at the end (untitled "island").
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$DIR/build_psp"
STAGE="$DIR/.scratch/psp_iso"
command -v mksfo >/dev/null 2>&1 || { echo "mksfo not found (pspdev)" >&2; exit 1; }

# Only the three variant outputs — never a broad glob, which would eat
# island_long.iso (and any other one-off test image) on every rerun.
rm -f "$BUILD/island_base.iso" "$BUILD/island_rr.iso" "$BUILD/island_uxa.iso"

for v in base uxa; do
    "$DIR/scripts/build_psp_iso.sh" >/dev/null
    mksfo "Island $v" "$STAGE/PSP_GAME/PARAM.SFO"
    flags=(-U)
    mkisofs -A "PSP GAME" -V "ISLAND_$v" -sysid "PSP GAME" -volset "" -p "" \
        -publisher "" "${flags[@]}" -o "$BUILD/island_$v.iso" "$STAGE"
    if [ "$v" = uxa ]; then
        python3 "$DIR/scripts/iso_uxa.py" "$BUILD/island_$v.iso"
    fi
    echo "build_psp_variants: wrote island_$v.iso ($(stat -f%z "$BUILD/island_$v.iso") bytes)"
done

"$DIR/scripts/build_psp_iso.sh" | tail -1
