#!/usr/bin/env python3
"""Append the retail-UMD 14-byte system-use field to every ISO9660 directory
record of an image, byte-for-byte as mastered on official discs:

    00 00 00 00 <8d dir | 0d file> 'U' 'X' 'A' 00 00 00 00 00 00

Directory extents in our layout are one 2048-byte block with ample slack, so
records grow in place; extents and the path tables are untouched. Aborts if a
directory would overflow its original size.

Usage: iso_uxa.py <image.iso>
"""
import struct
import sys

SU = bytes.fromhex("00000000")  # 4 zero bytes


def su_bytes(flags: int) -> bytes:
    return SU + bytes([0x8D if flags & 0x02 else 0x0D]) + b"UXA" + b"\x00" * 6


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    path = sys.argv[1]
    f = open(path, "r+b")
    f.seek(16 * 2048)
    pvd = f.read(2048)
    if pvd[1:6] != b"CD001":
        print("no PVD", file=sys.stderr)
        return 1

    def loc(buf):
        return struct.unpack_from("<I", buf, 2)[0], struct.unpack_from(
            "<I", buf, 10
        )[0]

    patched = 0
    dirs = 0

    def patch_dir(extent, size, path_str):
        nonlocal patched, dirs
        f.seek(extent * 2048)
        data = bytearray(f.read(size))
        out = bytearray()
        off = 0
        while off < len(data):
            rl = data[off]
            if rl == 0:
                # end of records: rest of the extent is slack (NUL), which
                # the grown records now eat into — rebuilt by the pad below
                break
            rec = bytes(data[off:off + rl])
            nlen = rec[32]
            name = rec[33:33 + nlen]
            pad = b"\x00" if (33 + nlen) % 2 else b""
            flags = rec[25]
            grown = bytearray(rec[:33 + nlen] + pad + su_bytes(flags))
            grown[0] = len(grown)  # record length byte must grow too
            out += grown
            if flags & 0x02 and nlen == 1 and name in (b"\x00", b"\x01"):
                pass
            elif flags & 0x02:
                de, ds = loc(rec)
                sub_path = (
                    path_str
                    if name in (b"\x00", b"\x01")
                    else path_str + name.decode("latin1") + "/"
                )
                pending.append((de, ds, sub_path))
            patched += 1
            off += rl
        if len(out) > size:
            raise SystemExit(
                f"{path}: dir {path_str} overflows {len(out)} > {size}"
            )
        out += b"\x00" * (size - len(out))
        f.seek(extent * 2048)
        f.write(out)
        dirs += 1

    root = pvd[156:190]
    root_loc = loc(root)
    pending = [(root_loc[0], root_loc[1], "/")]
    seen = set()
    while pending:
        extent, size, p = pending.pop()
        if (extent, size) in seen:
            continue
        seen.add((extent, size))
        patch_dir(extent, size, p)

    f.close()
    print(f"{path}: patched {patched} records across {dirs} directories")
    return 0


if __name__ == "__main__":
    sys.exit(main())
