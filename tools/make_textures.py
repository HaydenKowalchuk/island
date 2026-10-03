#!/usr/bin/env python3
"""island texture pipeline.

Generates the hard-edged PS1-style tiles used by the game into
assets/textures/. Every tile is:

  * seamless (toroidal value noise, so it can scroll/repeat)
  * palette-quantised from the locked palette in docs/art-direction.md
  * dithered with a 2x2 Bayer pattern where two tones meet
  * authored at native low resolution (no AA, no mipmaps; GL_NEAREST only)

Usage:
    python3 tools/make_textures.py [seed]

Sizes follow the spec: terrain/water 64x64, props/car 32x32.
docs/art-direction.md is the source of truth for the palette - keep in sync.
"""

import struct
import sys
import zlib
import math
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "textures"


def h2rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def shade_c(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)


def shade(name, k):
    return shade_c(PAL[name], k)


PAL = {
    # measured: user-sampled from the reference images (docs/art-direction.md)
    "sky_main": h2rgb("3BA8EC"),
    "water_mid": h2rgb("257276"),
    "foam": h2rgb("FFFFFF"),
    "grass_base": h2rgb("488226"),
    "car_body": h2rgb("B93427"),
    "windshield": h2rgb("2A1622"),
    # provisional
    "grass_dark": h2rgb("2F5C22"),
    "grass_light": h2rgb("69A83F"),
    "sand_dry": h2rgb("D9C48A"),
    "sand_wet": h2rgb("A8905C"),
    "rock": h2rgb("7A7268"),
    "wood": h2rgb("8A5A33"),
    "wood_dark": h2rgb("5E3D22"),
    "metal": h2rgb("9AA0A6"),
    "package": h2rgb("C8923C"),
}

# Derived tones stay locked to the palette by construction (multipliers only).
PAL["water_deep"] = shade("water_mid", 0.62)
PAL["water_shallow"] = shade("water_mid", 1.35)
PAL["car_body_lit"] = shade("car_body", 1.25)
PAL["rock_dark"] = shade("rock", 0.72)
PAL["rock_lit"] = shade("rock", 1.22)
PAL["car_seam"] = shade("car_body", 0.6)
PAL["metal_dark"] = shade("metal", 0.8)
PAL["metal_lit"] = shade("metal", 1.15)
PAL["sand_lit"] = shade("sand_dry", 1.12)
PAL["roof_lit"] = shade("wood", 1.15)
PAL["parcel_dark"] = shade("package", 0.75)
PAL["tape"] = shade("package", 1.45)

BAYER = ((0, 2), (3, 1))  # 2x2, values / 4


def rand2(seed, x, y):
    return zlib.crc32(struct.pack(">3i", seed, x, y)) / 4294967296.0


def smooth(t):
    return t * t * (3.0 - 2.0 * t)


def value_noise(x, y, size, period, seed):
    cells = size // period
    nx, ny = x / period, y / period
    ix, iy = int(nx) % cells, int(ny) % cells
    fx, fy = nx - int(nx), ny - int(ny)
    g = lambda a, b: rand2(seed, a, b)
    a00, a10 = g(ix, iy), g((ix + 1) % cells, iy)
    a01, a11 = g(ix, (iy + 1) % cells), g((ix + 1) % cells, (iy + 1) % cells)
    a = a00 + (a10 - a00) * smooth(fx)
    b = a01 + (a11 - a01) * smooth(fx)
    return a + (b - a) * smooth(fy)


def fbm(x, y, size, seed, octaves=2, period=8):
    v, amp, total, p = 0.0, 1.0, 0.0, period
    for o in range(octaves):
        v += amp * value_noise(x, y, size, p, seed + o * 101)
        total += amp
        amp *= 0.5
        p //= 2
    return v / total


def dither(c0, c1, t, x, y):
    return c1 if t * 4.0 >= BAYER[x % 2][y % 2] + 0.5 else c0


def pick(levels, t, x, y):
    n = len(levels) - 1
    fi = t * n
    i = int(fi)
    if i >= n:
        return levels[n]
    return dither(levels[i], levels[i + 1], fi - i, x, y)


def new_image(n):
    return Image.new("RGBA", (n, n))


def make_grass(n, seed):
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            t = fbm(x, y, n, seed, 2, 8)
            c = pick((PAL["grass_dark"], PAL["grass_base"], PAL["grass_light"]),
                     t * 1.15, x, y)
            if rand2(seed + 7, x, y) < 0.06:
                c = PAL["grass_light"]
            px[x, y] = c + (255,)
    return img


def make_water(n, seed):
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            # horizontal ripple bands, phase-wobbled by noise
            phase = 2.0 * math.pi * (2 * y / n) + (fbm(x, y, n, seed + 31, 2, 16) - 0.5) * 3.0
            t = 0.5 + 0.5 * math.sin(phase)
            c = pick((PAL["water_deep"], PAL["water_mid"], PAL["water_shallow"]), t, x, y)
            if rand2(seed + 11, x, y) < 0.02:
                c = PAL["water_shallow"]
            px[x, y] = c + (255,)
    return img


def make_sand(n, seed):
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            t = fbm(x, y, n, seed, 2, 16)
            # dry sand dominates; wet patches where the noise dips low
            w = max(0.0, min(1.0, 1.0 - t * 1.8))
            c = dither(PAL["sand_dry"], PAL["sand_wet"], w, x, y)
            if rand2(seed + 5, x, y) < 0.04:
                c = PAL["sand_lit"]
            px[x, y] = c + (255,)
    return img


def make_rock(n, seed):
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            t = fbm(x, y, n, seed, 2, 8)
            c = pick((PAL["rock_dark"], PAL["rock"], PAL["rock_lit"]), t, x, y)
            if rand2(seed + 13, x, y) < 0.08:
                c = PAL["rock_dark"]
            px[x, y] = c + (255,)
    return img


def make_wood(n, seed):
    img = new_image(n)
    px = img.load()
    plank_w = n // 4
    for y in range(n):
        for x in range(n):
            plank = x // plank_w
            tone = 0.85 + 0.3 * rand2(seed, plank, 0)
            c = shade_c(PAL["wood"], tone)
            if x % plank_w == 0:
                c = PAL["wood_dark"]
            elif value_noise(x * 3, y, n * 3, 4, seed + 9) > 0.62:
                c = shade_c(PAL["wood_dark"], 1.15)
            px[x, y] = c + (255,)
    return img


def make_roof(n, seed):
    img = new_image(n)
    px = img.load()
    tile = n // 4
    for y in range(n):
        row = y // tile
        xoff = tile // 2 if row % 2 else 0
        for x in range(n):
            if x % tile == 0 or y % tile == 0:
                c = PAL["wood_dark"]
            else:
                brick = (x + xoff) // tile
                c = PAL["roof_lit"] if (brick + row) % 2 else PAL["wood"]
            px[x, y] = c + (255,)
    return img


def make_metal(n, seed):
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            c = PAL["metal"]
            if x % (n // 2) == 0 or y % (n // 2) == 0:
                c = PAL["metal_dark"]
            quarter = (n // 4, n // 4)
            if (x % (n // 2), y % (n // 2)) == (quarter[0], quarter[1]):
                pass
            if ((x % (n // 2)) == quarter[0] and (y % (n // 2)) in (quarter[1], quarter[1] + 1)) or \
               ((y % (n // 2)) == quarter[1] and (x % (n // 2)) in (quarter[0], quarter[0] + 1)):
                c = PAL["metal_lit"]
            px[x, y] = c + (255,)
    return img


def make_hull(n, seed):
    # Gritty pixelated panel finish: high-frequency tone noise picks the
    # facet shade per pixel (reference images show grainy red panels, not
    # clean colour bands).
    img = new_image(n)
    px = img.load()
    for y in range(n):
        for x in range(n):
            t = value_noise(x, y, n, 2, seed + 41)
            c = pick((shade_c(PAL["car_body"], 0.72), PAL["car_body"],
                      PAL["car_body_lit"]), t, x, y)
            px[x, y] = c + (255,)
    return img


def make_parcel(n, seed):
    img = new_image(n)
    px = img.load()
    vt0, vt1 = int(n * 0.4), int(n * 0.6)      # vertical tape
    ht0, ht1 = int(n * 0.44), int(n * 0.56)    # horizontal tape
    for y in range(n):
        for x in range(n):
            c = PAL["package"]
            if x in (0, n - 1) or y in (0, n - 1):
                c = PAL["parcel_dark"]
            elif vt0 <= x < vt1 or ht0 <= y < ht1:
                c = PAL["tape"]
            px[x, y] = c + (255,)
    return img


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1337
    OUT.mkdir(parents=True, exist_ok=True)

    jobs = [
        ("grass_64.png", 64, make_grass),
        ("water_64.png", 64, make_water),
        ("sand_64.png", 64, make_sand),
        ("rock_64.png", 64, make_rock),
        ("wood_32.png", 32, make_wood),
        ("roof_32.png", 32, make_roof),
        ("metal_32.png", 32, make_metal),
        ("hull_32.png", 32, make_hull),
        ("parcel_32.png", 32, make_parcel),
    ]
    for name, size, fn in jobs:
        out = OUT / name
        fn(size, seed).save(out)
        print(f"wrote {out.relative_to(ROOT)}  ({size}x{size}, seed {seed})")


if __name__ == "__main__":
    main()
