# Art direction

Transcribed from two reference screenshots by the user (the tooling cannot view
images, so this document is the contract for all content work). Values are
provisional until the screenshots are re-checked; keep edits here rather than in
scattered source constants.

Reference read: PS1-era low-poly, hard-edged textures. A low, red car/boat
hybrid carves across blue-green polygonal water with a sharp white spray wake.
Sloping green islands dot the horizon under a bright blue sky with chunky
pixelated cumulus clouds. Wooden docks with a post and a small tank sit at the
water's edge. UI chrome in the frames is video-player overlay, not game art.

## Palette

| Name | Hex | RGB (0-1) | Use |
| --- | --- | --- | --- |
| sky_zenith | `#2E6FD6` | 0.18, 0.44, 0.84 | top of sky gradient |
| sky_horizon | `#8FC7EA` | 0.56, 0.78, 0.92 | horizon band, fog colour |
| cloud_lit | `#F2F6F8` | 0.95, 0.96, 0.97 | cloud tops |
| cloud_shade | `#C2D2DC` | 0.76, 0.82, 0.86 | cloud undersides |
| water_deep | `#0E454C` | 0.05, 0.27, 0.30 | far water |
| water_mid | `#14646B` | 0.08, 0.39, 0.42 | mid water |
| water_shallow | `#25939A` | 0.14, 0.58, 0.60 | near shore |
| foam | `#EDF6F6` | 0.93, 0.96, 0.96 | wake, breaking water |
| grass_base | `#4A8A34` | 0.29, 0.54, 0.20 | island surface |
| grass_dark | `#2F5C22` | 0.18, 0.36, 0.13 | shaded slopes |
| grass_light | `#69A83F` | 0.41, 0.66, 0.25 | sunlit slopes |
| tree_canopy | `#3B7A2A` | 0.23, 0.48, 0.16 | blocky trees |
| sand_dry | `#D9C48A` | 0.85, 0.77, 0.54 | beach |
| sand_wet | `#A8905C` | 0.66, 0.56, 0.36 | waterline |
| rock | `#7A7268` | 0.48, 0.45, 0.41 | cliffs, boulders |
| wood | `#8A5A33` | 0.54, 0.35, 0.20 | dock, posts, crates |
| wood_dark | `#5E3D22` | 0.37, 0.24, 0.13 | wood shading |
| metal | `#9AA0A6` | 0.60, 0.63, 0.65 | tank, fittings |
| car_body | `#C42B22` | 0.77, 0.17, 0.13 | hover car hull |
| car_body_lit | `#E0392C` | 0.88, 0.22, 0.17 | hull highlight facets |
| windshield | `#1A2430` | 0.10, 0.14, 0.19 | dark glass |
| package | `#C8923C` | 0.78, 0.57, 0.24 | parcel |

## Textures

- Terrain tiles 64x64 (grass, sand, rock); water 64x64; wood/metal/props 32x32
  or 64x64; car panels 32x32.
- `GL_RGBA`, `GL_NEAREST` min and mag, **no mipmaps** — mip shimmer breaks the
  look.
- Hard edges: no anti-aliasing in the texture itself, palette-quantised colours,
  visible dithering where a gradient is needed.
- Authored procedurally in `src/textures.c` (see island1-802h) so the repo has
  no binary assets and the palette above can be tweaked in one place.

## Camera

- Chase camera behind and above the car. Offset ~8 m back, ~3.5 m up, look-ahead
  ~6 m. FOV 60°, pulled back and widened slightly with speed.
- Reference frame 2 sits low, close to the water surface, with the car banking
  hard — the camera is allowed to drop near spray height during turns.

## Lighting and fog

- One key light from the upper front-left, warm-neutral; one cool fill from
  behind. Most colour comes from texture and vertex colour, not light.
- Linear fog, colour = `sky_horizon`, start ~60 m, end ~220 m. Ocean plane ends
  before fog end so no plane edge is ever visible.

## Silhouettes

- Car: low wedge, faceted panels, dark windshield, no curves.
- Trees: trunk box plus one or two angled box/cone canopy blocks.
- Buildings: rectangular boxes, gabled roofs, hard shading.
- Characters: blocky box humanoids (see island1-urkr).
- Clouds: chunky, pixel-edged, near the horizon.

## Presentation

- PS1-era feel: vertex jitter, affine-ish texture warping, low internal render
  resolution with NEAREST upscale, heavy fog. See island1-a440.

## HUD

Not derivable from the screenshots: the visible chrome is the video player's.
Game HUD (package status, compass, speed) is specced separately in island1-n7to.
