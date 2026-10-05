# Dreamcast render backend (PowerVR2)

Contract for the future Dreamcast backend (island1-fo7c). It reads the core
RenderFrame (see [render.md](render.md)) and issues native draws through
KallistiOS's PVR API. Hardware facts below were verified against the
KallistiOS sources (`kernel/arch/dreamcast/{hardware,include}/pvr`, `examples`)
— follow up there when implementing.

## Hardware overview

- PowerVR2 (CLX2): **tile-based deferred renderer**. The Tile Accelerator
  (TA) accepts vertex streams, buckets them into 32x32 screen tiles and sorts
  them; the ISP/TSP then rasterises per tile. Sorting is the depth strategy —
  there is no general Z-first pipeline like GL.
- Five polygon lists, chosen **at submit time per draw** (KOS
  dc/pvr/pvr_header.h):

| list | use |
| --- | --- |
| PVR_LIST_OP_POLY | opaque polygons |
| PVR_LIST_OP_MOD | opaque modifier volumes |
| PVR_LIST_TR_POLY | translucent polygons (sorted) |
| PVR_LIST_TR_MOD | translucent modifier volumes |
| PVR_LIST_PT_POLY | punch-through — alpha test |

- Per-polygon-header state (pvr_poly_hdr_t / pvr_poly_cxt_t):
  - culling: PVR_CULLING_NONE / SMALL / CCW / CW (screen-space winding)
  - user clip: PVR_USERCLIP_DISABLE / INSIDE / OUTSIDE (scissor-like)
  - depth: comparison (PVR_DEPTHCMP_*) + write, per header
  - fog: PVR_FOG_TABLE / VERTEX / TABLE2 / DISABLE, per header
  - blend: src/dst modes with explicit enable flags (KOS defaults DISABLE)
  - texture env: PVR_TXRENV_REPLACE / MODULATE / DECAL / MODULATEALPHA
    (REPLACE: px = ARGB(tex); MODULATE: px = A(tex) + RGB(col)*RGB(tex))
  - filter: PVR_FILTER_NEAREST / BILINEAR (+ mipmaps)
  - specular (offset colour oargb), colour clamp, gouraud shading enable
- 8 MB VRAM; max texture 1024x1024; formats RGB565, ARGB1555, ARGB4444,
  palettized (4/8bpp CLUT), VQ compression, twiddled addressing, YUV422.
- **No stencil buffer** — modifier volumes are the idiomatic replacement
  (shadow volumes, per-pixel clipping: `pvr_modifier_commit_zclip`,
  PVR_MODIFIER_CHEAP_SHADOW).
- Native sprites (`pvr_sprite_compile`) and native triangle strips with
  auto/manual strip length.
- Display: 640x480 VGA or 640x240p/480i; the PS1 look wants a low internal
  resolution anyway.

## Mapping RenderFrame → PVR

| RenderFrame / RDraw | Dreamcast |
| --- | --- |
| `composite` = OPAQUE | PVR_LIST_OP_POLY; expand 16-bit indices into triangle strips |
| `composite` = TRANSLUCENT | PVR_LIST_TR_POLY (HW sorts; no manual sorting needed from us) |
| `composite` = CUTOUT (island1-qb9x) | PVR_LIST_PT_POLY — punch-through is native alpha discard; no blending, no sorting issues |
| `composite` = OVERLAY | PVR_LIST_PT_POLY — shares PT with CUTOUT; PT renders after translucent, which is what "over everything" needs |
| R_QUADS | **no quad primitive** — expand each quad to a 2-triangle strip CPU-side |
| R_LINES | 2-vertex line strips |
| billboards | already world-space triangles — submit directly |
| screen_space 2D | ortho-transform CPU-side into screen-space verts; HUD rect/text as tris or sprites |
| screen_space depth | OVERLAY → z 1.0 (nearest); other screen draws → znear/fog_end (farthest non-zero; z = 0 is infinity to the ISP) |
| fog | pvr_fog_table_linear + pvr_fog_table_color over the frame's start/end; per-header fog type |
| tint | multiply into vertex colours on CPU at submit |

List choice and screen-space depth come straight from `RDraw::composite` — see
island1-m4g4 and docs/render.md. Before that they were guessed from `blend` and
`pass == R_PASS_HUD`, which made the sky (screen-space, but scene) and the HUD
(screen-space, overlay) indistinguishable to the backend.
| cull | polygon-header culling: PVR_CULLING_CCW/CW to match our GL_CCW winding |
| scissor (island1-4t0s) | user clip in the polygon header (PVR_USERCLIP_INSIDE) |
| tex_env (island1-01nc) | MODULATE → PVR_TXRENV_MODULATE; REPLACE → PVR_TXRENV_REPLACE; ADD → blend (src ONE + dst ONE, blend enabled) |
| depth_write | per-header depth write + comparison |

Transforms: the TA accepts vertices, not matrices — the backend transforms
verts on the CPU (world matrix + camera view/proj; KOS `mat_` helpers or
cglm). There is no hardware matrix stack to load, unlike the desktop backend.

## Constraints & gotchas

1. **Near-plane clipping is the big one.** The TA produces garbage for
   vertices behind the eye (w <= 0). Never submit big polygons crossing the
   near plane — subdivide. The ocean's 10-unit cells are already safe; keep
   that policy for terrain. CPU-clip anything that can cross (wake ribbon,
   billboards) if it grows.
2. **CPU frustum culling is mandatory** (island1-mc2v). HW clipping happens
   too late — polys outside the frustum still burn TA vertex budget, tile
   list space and transform time. Cull before recording.
3. **Affine texture warp is NOT native.** The PVR is perspective-correct.
   Emulate the PS1 wobble by subdividing textured polys (small perspective
   error reads as affine grain) or quantising UVs per-vertex on CPU.
4. **Punch-through = alpha test**: submit alpha-tested draws to PT_POLY — no
   blending, no transparent-sorting artefacts, sorts with opaque. Keep
   blended draws on TR_POLY. A draw cannot be both (they are different
   lists) — so `R_COMPOSITE_CUTOUT` and `R_COMPOSITE_TRANSLUCENT` are mutually
   exclusive per draw, and the core declares which rather than leaving the
   backend to infer it from `blend`.
5. **Blend is per-header and opt-in** (KOS context defaults leave
   src_enable/dst_enable = PVR_BLEND_DISABLE) — set both when translucency is
   wanted.
6. **Tint on CPU**: multiply into vertex colours at submit. Pack colours to
   ARGB1555/4444 after tinting — the quantisation matches the PS1 15-bit look
   on purpose. Enable colour clamp (PVR_COLOR_CLAMP) to avoid overflow.
7. **Vertex colours are the only lighting** — baked in core meshes (see the
   lighting decision in render.md); gen.shading (Gouraud) interpolates them.
8. **No stencil, no multitexture, no shaders, no index buffers, no quads.**
9. Twiddled + VQ textures are the fast path; upload once at load, never per
   frame.
10. **Keep polys small** — a general rule for the TA; big world-space polys
    that cross the eye plane are the main source of glitches.

## Performance budget

- Vertex throughput is the bottleneck: cull on CPU (island1-mc2v), keep polys
  small, batch into strips (the header's auto_strip_len helps).
- Tile lists are small — the 512 draws/frame cap (island1-41ct) is generous
  here, but each draw becomes a header + strip stream, so batching recorded
  verts (text, wake) matters.
- 8 MB VRAM: 32/64px tiles per docs/art-direction.md are tiny; VQ-compress if
  it ever grows.

## Fog

The PVR supports per-pixel table fog and vertex fog per polygon header
(PVR_FOG_TABLE / VERTEX / TABLE2 / DISABLE), and KOS builds fog tables from an
inverse-w depth table for perspectively correct linear fog
(pvr_fog_tables.h). Available: `pvr_fog_table_linear` / `_exp` / `_exp2` /
`_custom`, `pvr_fog_table_color` (the fog colour), `pvr_fog_far_depth`, plus
the PVR_FOG_DENSITY register.

For the game: build the table with `pvr_fog_table_linear` over the frame's
fog_start/fog_end and set `pvr_fog_table_color` to the sky colour. EXP modes
are reachable but we never use them (linear only — island1-pqhi).

## Done when (island1-fo7c)

The desktop game's frame renders 1:1 in an emulator (redream/NullDC) or on
hardware: same passes, same order, same fog/blend/cull behaviour, stats
overlay matching.
