# Art direction

Transcribed from two reference screenshots by the user (the tooling cannot view
images, so this document is the contract for all content work). Keep edits here
rather than in scattered source constants. Palette entries marked "measured" are
hex values the user sampled from the reference images (2026-10-03); the rest are
provisional or derived (by multiplier) from the measured set.

Reference read: PS1-era low-poly, hard-edged textures. A low, red car/boat
hybrid carves across blue-green polygonal water with a sharp white spray wake.
Sloping green islands dot the horizon under a bright blue sky with chunky
pixelated cumulus clouds. Wooden docks with a post and a small tank sit at the
water's edge. UI chrome in the frames is video-player overlay, not game art.

## Palette

| Name | Hex | Source | Use |
| --- | --- | --- | --- |
| sky_main | `#3BA8EC` | measured | vibrant sky blue, gradient top |
| sky_horizon | `#89CBF4` | derived (sky_main +40% white) | horizon band, fog colour |
| cloud | `#FFFFFF` | measured | puffy cumulus bodies |
| cloud_shade | `#B9C7CE` | provisional | cloud undersides |
| grass_base | `#488226` | measured | island / hillside surface |
| grass_dark | `#2F5C22` | provisional | shaded slopes |
| grass_light | `#69A83F` | provisional | sunlit slopes |
| tree_canopy | `#3B7A2A` | provisional | blocky trees |
| water_deep | `#164649` | derived (water_mid x0.62) | far water |
| water_mid | `#257276` | measured | deep ocean water |
| water_shallow | `#31999F` | derived (water_mid x1.35) | near shore |
| foam | `#FFFFFF` | measured | splash, breaking water |
| sand_dry | `#D9C48A` | provisional | beach |
| sand_wet | `#A8905C` | provisional | waterline |
| rock | `#7A7268` | provisional | cliffs, boulders |
| wood | `#8A5A33` | provisional | dock, posts, crates |
| wood_dark | `#5E3D22` | provisional | wood shading |
| metal | `#9AA0A6` | provisional | tank, fittings |
| car_body | `#B93427` | measured | hover car hull |
| car_body_lit | `#E74130` | derived (car_body x1.25) | hull highlight facets |
| windshield | `#2A1622` | measured | dark windshield tint |
| package | `#C8923C` | provisional | parcel |

## Textures

- Terrain tiles 64x64 (grass, sand, rock); water 64x64; wood/metal/props 32x32
  or 64x64; car panels 32x32.
- `GL_RGBA`, `GL_NEAREST` min and mag, **no mipmaps** — mip shimmer breaks the
  look.
- Hard edges: no anti-aliasing in the texture itself, palette-quantised colours,
  visible dithering where a gradient is needed.
- Authored by `tools/make_textures.py` (seeded, reproducible): palette-quantised
  seamless PNGs live in `assets/textures/`. Regenerate with
  `python3 tools/make_textures.py [seed]` after changing the palette.
- Packaged by meson: the `copy-assets` target copies `assets/` next to the built
  binary (`<build>/src/assets`); the loader finds them via `ASSET_DIR`
  (set by `run.sh`; see island1-9rmh).
- Hull texture is gritty grainy red (high-frequency tone noise), not clean
  colour bands — matches the reference's pixelated panel finish.

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

- Car: low, flat-topped profile; sharply sloping front hood; distinct blocky
  cabin enclosed by the dark windshield; faceted panels, no curves. Gritty
  pixelated texture on the red body panels (PS1 affine-mapping grain).
- Wake/splash: sharp jagged vertical spikes and towering triangular plumes
  shooting up and out behind and beside the vehicle; stark asymmetric white
  trail against the dark water. Solid chunky pixel blocks, high-contrast flat
  white — no fluid gradient alpha; stippled pixel noise along outer edges to
  read as roaring spray (drives island1-5stj).
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
