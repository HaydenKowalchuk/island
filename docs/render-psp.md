# PSP render backend (GE via sceGu)

Contract for the PSP backend (island1-gi02). It reads the core RenderFrame (see
[render.md](render.md)) and issues native draws through the PSP's GE via
pspsdk's sceGu/sceGum. Hardware facts below were verified against the pspsdk
sources (github.com/pspdev/pspsdk, `src/gu`).

## Implementation status (island1-gi02)

Backend lives in `src/backend/psp/` and is built as a second meson target:

- `main.c` — module boilerplate (`psp_setup.h`), fixed 60 Hz loop, pad polling.
- `render_gu.c` — GE flusher: `sceGuSetMatrix(GU_PROJECTION/VIEW)` fed directly
  from `frame->proj`/`frame->view` (cglm and `sceGumPerspective` share the same
  column-major layout, so `sceGum` is only used once at init for the identity and
  the `[0,1]^2` screen-space ortho). Mesh draws carry `d->world` as `GU_MODEL`.
- `texture_gu.c` — stb_image PNG → `GU_PSM_8888` upload (ABGR), NEAREST.
- `input_psp.c` — pad + nub → the core input contract.

Build: `meson setup build-psp --buildtype=release -Dplatform=psp
--cross-file mips-allegrex-psp` then `meson compile -C build-psp`; it produces
`src/EBOOT.PBP` (plus `EBOOT.BIN`/`BOOT.BIN`) via psp-fixup-imports → psp-prxgen
→ pack-pbp.

Decisions taken while implementing, in priority order:

- Framebuffers are **`GU_PSM_5650`** (double-buffered, 512-pixel stride) so the
  GE dither is visible; depth is `GU_PSM_4444`. This is the only VRAM user
  besides the display list — textures live in main RAM (cache-flushed) because
  the one real texture is tiny.
- Depth uses the reversed convention: `sceGuDepthRange(65535, 0)`,
  `sceGuClearDepth(0)`, `GU_GEQUAL` — larger value = nearer, which is what GU
  clears to.
- `R_QUADS` and `R_LINES` are copied/expanded into scratch vertex buffers in the
  GE's native packed formats (`GU_TEXTURE_32BITF | GU_COLOR_8888 |
  GU_VERTEX_32BITF`, plus `GU_INDEX_16BIT` for meshes).
- Lighting stays baked (vertex colours); `GU_LIGHTING` is never enabled.
- Fog **is** hardware: `sceGuFog(fog_start, fog_end, sky)` + `GU_FOG`. This
  supersedes the "CPU vertex-colour fog" note in island1-gi02's description.
- Screen-space HUD/sky uses the GE's `GU_TRANSFORM_3D` with a `[0,1]` ortho and
  depth test disabled, matching GL's bottom-left origin.

Verified in PPSSPP: the frame matches the desktop build (passes, winding, depth,
fog, textures and screen-space HUD all correct), so the matrix/winding/depth
sign conventions above are confirmed as-is.

## Hardware overview

- GE = Graphics Engine, driven through sceGu (display lists built in memory)
  + sceGum (matrix helpers). No OpenGL, no GLSL.
- 480x272 display; 32 MB main RAM + 2 MB dedicated VRAM. Framebuffers, depth
  buffer, textures and display lists share VRAM.
- 16-bit colour formats GU_PSM_5650 / 5551 / 4444 — matches the PS1 15-bit
  look directly; native 4x4 dither matrix (GU_DITHER state, `sceGuSetDither`;
  sceGuStart installs a default matrix).
- Max texture 512x512; swizzled textures are the fast path; one texture stage
  (no multitexture); no shaders; no VBOs and no real index buffers —
  `sceGuDrawArray` takes a vertex pointer (indices optional;
  count <= 65535).
- Flexible vertex formats (VTYPE flags): each attribute is independently
  f32 / s16 / u16 / s8 / u8, e.g.
  `GU_TEXTURE_16BIT | GU_COLOR_4444 | GU_VERTEX_16BIT | GU_TRANSFORM_2D`.
  Keep the native vertex struct small — the GE accepts packed attributes.
- 16-bit depth buffer; `sceGuDepthFunc(GU_GEQUAL)` in the samples;
  `sceGuDepthRange(int near, int far)` exists.

## Verified pspsdk surface (src/gu)

Primitives (pspgu.h) — **there is no quad primitive**:

| value | primitive |
| --- | --- |
| 0 | GU_POINTS |
| 1 | GU_LINES |
| 2 | GU_LINE_STRIP |
| 3 | GU_TRIANGLES |
| 4 | GU_TRIANGLE_STRIP |
| 5 | GU_TRIANGLE_FAN |
| 6 | GU_SPRITES — "filled blocks", 2 vertices per primitive |

`sceGuDrawArray` asserts `prim >= GU_POINTS && prim <= GU_SPRITES` — R_QUADS
must be expanded to two triangles each on the CPU.

States (pspgu.h): GU_ALPHA_TEST, GU_DEPTH_TEST, GU_SCISSOR_TEST,
GU_STENCIL_TEST, GU_BLEND, GU_CULL_FACE, GU_DITHER, GU_FOG, GU_CLIP_PLANES,
GU_TEXTURE_2D, GU_LIGHTING.

Key functions:

- `sceGuAlphaFunc(int func, int value, int mask)` — GU_NEVER..GU_GEQUAL; the
  sprite sample uses `sceGuAlphaFunc(GU_GREATER, 0, 0xff)`
- `sceGuBlendFunc(int op, int src, int dst, unsigned srcfix, unsigned dstfix)`
  — ops GU_ADD / GU_SUBTRACT / GU_REVERSE_SUBTRACT / GU_MIN / GU_MAX / GU_ABS;
  factors GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, GU_FIX (+packed fix), ...
  (`(Cs*Bs) op (Cd*Bd)`)
- `sceGuFog(float near, float far, unsigned color)` — **linear only**, no
  mode or density parameter
- `sceGuScissor(int x, int y, int w, int h)` — asserts the region is within
  480x272
- `sceGuSetDither(const ScePspIMatrix4 *)` — 4x4 integer matrix
- GU_SPRITES also works with GU_TRANSFORM_3D (the sprite sample draws 3D
  billboards with it)

## Mapping RenderFrame → GE

| RenderFrame / RDraw | PSP |
| --- | --- |
| mesh draws (R_TRIANGLES) | GU_TRIANGLES / triangle strips, packed VTYPE vertex format, transformed CPU-side or via gum matrices |
| R_QUADS | **no quad primitive** — expand to 2 tris each CPU-side |
| R_LINES | GU_LINES |
| billboards | already world-space triangles — submit directly |
| screen_space rects | GU_SPRITES (native 2-vertex rect) or GU_TRANSFORM_2D tris |
| 2D text | batched glyph quads → tris, or GU_SPRITES runs |
| fog | `sceGuFog(fog_start, fog_end, sky colour)` — linear |
| alpha_test (island1-qb9x) | native: `sceGuEnable(GU_ALPHA_TEST)` + `sceGuAlphaFunc(GU_GREATER, ref*255, 0xff)` |
| scissor (island1-4t0s) | native: `sceGuScissor` + `sceGuEnable(GU_SCISSOR_TEST)` |
| blend | `sceGuEnable(GU_BLEND)` + `sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0)` |
| tex_env (island1-01nc) | `sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA)`; REPLACE/ADD map to TFX variants or blend ops |
| tint | multiply into vertex colours on CPU at submit (quantise after — packed formats) |
| cull | `sceGuFrontFace(GU_CCW)` + `sceGuEnable(GU_CULL_FACE)` — our meshes are GL_CCW wound |
| depth_write | pixel mask / depth write state |
| clear | `sceGuClear` (uses GU_SPRITES internally) |

Transforms: two options — transform on CPU (like the DC backend) or use
sceGum/sceGuSetMatrix. Verify matrix conventions against the samples at
implementation time: pspsdk samples set `sceGuOffset(2048 - w/2, 2048 - h/2)`
+ `sceGuViewport(2048, 2048, w, h)` and many use GU_CW front face (their
meshes are wound CW); our cglm matrices and CCW winding must be mapped
deliberately, not copied from samples.

## Constraints & gotchas

1. **No quad primitive** — R_QUADS expands to triangles CPU-side; the wake
   ribbon, sky and rects all arrive as quads.
2. **Vertex budget** — CPU frustum culling before recording matters most here
   (island1-mc2v): the GE still vertex-processes submitted polys even when
   they end up clipped, so culling before recording is the only culling that
   saves real work.
3. **One texture stage** — multitexture must never be used (the API is
   already compliant).
4. **Texture size 512 max**, swizzle on upload; NEAREST filter per the art
   direction, no mipmaps (`sceGuTexImage` level params 0, 0).
5. **VRAM is tight (2 MB)** — double-buffered framebuffers + 16-bit depth +
   display lists + textures share it; keep textures 32/64px per
   docs/art-direction.md and keep display lists lean (batched text/wake).
6. **Vertex colour lighting only** — baked in core (lighting decision in
   render.md); GU_LIGHTING/GU_LIGHT0 stay off (island1-gi02 decision:
   CPU-baked colours so the look matches desktop exactly).
7. **Dither is free** — 16-bit colour + the GE's dither matrix gives the PS1
   dithered look natively (GU_DITHER; sceGuStart installs a default matrix).
8. **Winding**: samples use GU_CW; our meshes are CCW — set
   `sceGuFrontFace(GU_CCW)` explicitly and verify culling in the emulator.
9. **Affine texture warp is not native** — the GE is perspective-correct by
   default; the wobble is emulated (subdivide textured polys / quantise UVs
   on CPU), same approach as the DC backend.
10. **Stencil**: GU exposes a stencil-test state, but the framebuffer formats
    carry no usable stencil plane — treat stencil as unsupported; don't build
    effects that need it.

## Display list / performance budget

- The GE runs display lists built in VRAM: batch draws (the API already
  batches text/wake) to keep lists small; `sceGuGetMemory` per frame.
- The 512 draws/frame cap (island1-41ct) is generous; each draw becomes VTYPE
  + state changes + vertices, so consecutive same-state draws are cheap if
  the backend coalesces.
- Frustum culling (island1-mc2v) + the fog horizon keep the vertex count low.

## Fog

The GE fog is linear only: `sceGuFog(near, far, color)` with GU_FOG enabled —
no mode or density parameter exists in the API (verified: src/gu/sceGuFog.c).
This matches the game's linear fog contract exactly (frame fog_start/fog_end,
colour = sky). If the fog mode pellet (island1-pqhi) ever adds EXP to the
frame, the PSP backend ignores/rejects it — the DC backend could honour it
(`pvr_fog_table_exp`) but the PSP cannot.

## Done when (island1-gi02)

The desktop game's frame renders 1:1 on PPSSPP or hardware: same passes, same
order, same fog/blend/cull behaviour, stats overlay matching.
