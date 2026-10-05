# Render — desktop (OpenGL 1.5 fixed-function)

The rendering contract for island, audited 2026-10-03 against the OpenGL 1.5
fixed-function pipeline and the Dreamcast/PSP targets (PS1-style low-poly
graphics). Companion docs: [render-dc.md](render-dc.md),
[render-psp.md](render-psp.md). Gaps found by the audit are tracked as pellets
and listed at the bottom of this file.

## Architecture

Strict layering (island1-jimd): core never issues GL calls; the backend owns
all GL state.

```
src/core/game.c, mesh.c      content + passes; record primitives
        |
        | r_*()              RenderAPI (src/core/render.h)
        v
RenderFrame                  plain data: verts + ordered draws + camera/fog
                             (src/core/render.c)
        |
        | r_frame()
        v
src/backend/desktop/         gfx_gl.c (GL state manager) + render_gl.c (flusher)
```

The backend holds no game state. It initialises fixed-function GL once
(`render_init`), then every frame turns the recorded frame into gfx/GL calls
(`render_present`). Because the recorded frame is plain data, the PSP and
Dreamcast backends can read the same structure and issue native draws — that
is the whole point of the split (island1-6rg2).

## GL version compliance

`main.c` requests a legacy compatibility context
(`GLFW_CONTEXT_VERSION_MAJOR=2`, `GLFW_CONTEXT_VERSION_MINOR=1`,
`GLFW_OPENGL_ANY_PROFILE`); macOS delivers 2.1, a superset of GL 1.5. GLAD is
vendored under `subprojects/glad` with `gl=2.1`, compatibility profile.

In use (all GL 1.2/1.5 legal):

- matrix stacks: `glMatrixMode`, `glLoadIdentity`, `glMultMatrixf`
- immediate mode: `glBegin`/`glEnd` (small recorded batches)
- client-state vertex arrays: `glVertexPointer`, `glColorPointer`,
  `glTexCoordPointer`, `glDrawElements` with `GL_UNSIGNED_SHORT` indices
- `glTexImage2D` for 32/64px tiles (NEAREST, no mipmaps)

Banned (breaks the console story or adds nothing): VBOs, VAOs, shaders,
multitexture, stencil, framebuffer objects.

## Frame structures

### RVertex — 9 floats, 36 bytes (src/core/render.h)

| field | meaning |
| --- | --- |
| x, y, z | position (world space unless the draw is `screen_space`) |
| r, g, b, a | colour |
| u, v | texture coordinates |

Interleaved on purpose: the desktop mesh path hands one pointer to GL with
strides; console backends can repack into their native vertex formats from the
same struct.

### RDraw — one draw (ordered in a single list)

| field | meaning | desktop mapping |
| --- | --- | --- |
| `prim` | R_TRIANGLES / R_LINES / R_QUADS | `gl_mode()` → GL_TRIANGLES / GL_LINES / GL_QUADS |
| `first`, `count` | contiguous run in `frame->verts` | immediate-mode loop |
| `mesh`, `world` | reusable Mesh + column-major transform | client arrays + `glDrawElements(GL_TRIANGLES, u16)` + `glMultMatrixf` |
| `composite` | R_COMPOSITE_OPAQUE / _CUTOUT / _TRANSLUCENT / _OVERLAY | see below — no GL equivalent, DEAD today |
| `screen_space` | [0,1] ortho projection (HUD, sky) | `gfx_screen_begin_unit()` |
| `lit` | vertex lighting | DEAD — always 0 (island1-agob) |
| `fog` | per-draw fog on/off | GL_FOG + frame fog start/end/colour |
| `blend` | alpha blend | GL_BLEND, (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) |
| `depth_write` | depth mask | `glDepthMask` |
| `cull` | R_CULL_NONE / R_CULL_BACK | GL_CULL_FACE |
| `textured` | GL_TEXTURE_2D + per-mesh bind | `gfx_bind_texture()` |
| `tint[4]` | per-draw colour multiplier | scaled scratch copy (meshes) / immediate multiply (recorded verts) |
| `pass` | RPass, stamped by `r_draw()` | F3 stats/overlay only |

#### `composite` — declared list intent (island1-m4g4)

`composite` says what a draw **is**, so a backend picks a native list instead of
inferring one from `blend` or the pass enum:

| class | meaning | Dreamcast | PSP |
| --- | --- | --- | --- |
| `R_COMPOSITE_OPAQUE` | solid, sorts with the scene, writes depth | `PVR_LIST_OP_POLY` | depth test + write |
| `R_COMPOSITE_CUTOUT` | alpha-tested, per-pixel discard | `PVR_LIST_PT_POLY` | `GU_ALPHA_TEST` (island1-qb9x) |
| `R_COMPOSITE_TRANSLUCENT` | alpha-blended over the scene, back to front | `PVR_LIST_TR_POLY` | blend, no depth write |
| `R_COMPOSITE_OVERLAY` | HUD/2D, drawn over everything | `PVR_LIST_PT_POLY` | no depth test/write |

OVERLAY has no PVR list of its own and shares PT with CUTOUT: PT renders after
translucent, which is what "over everything" needs. The core states the intent;
which bin achieves it is the backend's business, so no PVR/GE concept appears in
`render.h`.

`screen_space` stays orthogonal — it only picks the projection. The sky is
screen-space but still OPAQUE (it must sort and depth-test behind the island),
while HUD rects and text are screen-space AND OVERLAY. Before this, DC had to
guess both from `pass == R_PASS_HUD`, which meant a screen-space scene draw was
indistinguishable from a HUD one.

The desktop ignores `composite`: every class already sets `blend` and
`screen_space` consistently, so desktop output is unchanged.

### RenderFrame

- width/height (framebuffer pixels, used by 2D text placement)
- clear colour, view/proj (column-major, cglm-compatible)
- fog: enabled, colour, start, end
- `verts[R_MAX_VERTS]` + `draws[R_MAX_DRAWS]` + stats
  (`stat_verts`, `stat_draws`, per-pass counters for the F3 overlay)

Caps: `R_MAX_VERTS 16384`, `R_MAX_DRAWS 512` (island1-41ct).

## Desktop flush pipeline

`render_present(fb_w, fb_h)` (src/backend/desktop/render_gl.c):

1. `glViewport` + `glClearColor` + `glClear` (colour + depth)
2. `set_projection(frame->proj)`, `set_modelview(frame->view)`
3. for each draw in order:
   - **mesh path**: push GfxState, bind texture, `glPushMatrix` +
     `glMultMatrixf(world)`, enable client states,
     `glVertexPointer`/`glColorPointer`/`glTexCoordPointer` on the (possibly
     tint-scaled) interleaved array, `glDrawElements(GL_TRIANGLES, u16)`, pop.
   - **recorded path**: push GfxState, (if `screen_space`: begin the [0,1]
     ortho unit), `glBegin` + per-vertex `glColor4f`/`glVertex3f`
     (+`glTexCoord2f`), `glEnd`, pop.

The gfx state manager (gfx_gl.c) diffs GfxState against current GL state so
consecutive draws with identical flags cost nothing: lighting, fog, texture,
blend, depth mask, cull, tex env, filter, fog start/end/colour. `gfx_push` /
`gfx_pop` save and restore.

A 1x1 white texture is bound by default so GL_MODULATE with white is the
identity — untextured draws keep their vertex colour instead of sampling the
incomplete default texture object (which renders nothing).

## Recording API reference

| call | emits |
| --- | --- |
| `r_frame_begin()` | resets the frame; clear = sky; identity view/proj |
| `r_clear(r,g,b,a)` | override the clear colour |
| `r_viewport(w,h)` | framebuffer size (for 2D text placement) |
| `r_camera(view, proj)` | column-major matrices |
| `r_fog(...)` | fog enabled/colour/start/end |
| `r_vertex(...)` | one RVertex; returns index or -1 when full |
| `r_draw(draw)` | one RDraw, stamped with the current RPass |
| `r_mesh(mesh, world, tint)` | reusable mesh draw (triangles, fog, depth write, backface cull) — OPAQUE |
| `r_billboard(pos, w, h, color)` | camera-facing quad as 4 world-space triangles — TRANSLUCENT |
| `r_rect_2d(x, y, w, h, color)` | screen-space alpha-blended quad — OVERLAY |
| `r_ribbon(pts, n, width, color)` | flat world-space strip (wake/road), batched — TRANSLUCENT |
| `r_text_2d(x, y, scale, s, color)` | batched 8x8 glyph quads (one draw per string) — OVERLAY |
| `r_pass(p)` / `r_pass_name(p)` | current pass tag for stats |
| `r_composite_name(c)` | short label for a composite class, e.g. "OVERLAY" |

## Lighting model (decision)

User direction (2026-10-03): the game takes place in bright sun and targets
older retro hardware, so the lighting model stays simple — likely no runtime
lighting at all, or if any, computed per-vertex/per-face without smooth
(per-pixel) shading.

Consequences:

- Colour comes from texture + baked vertex colour + per-draw tint. Flat
  shading is baked at mesh build time (`mesh_add_box` shades faces
  top-brightest, bottom-darkest) — no runtime lighting needed on any backend.
- The GL fixed-function lighting pipeline is NOT the path. `setup_lights()`
  (gfx_gl.c) and the `lit` flag are dead plumbing today and will be removed
  (island1-agob). If runtime lighting is ever added it will be CPU-side,
  baking light into vertex colours before recording — the RenderAPI stays
  unchanged (no normals in the frame), and consoles get it for free.

## Audit — what exists (2026-10-03)

- **View frustum: yes.** Gribb-Hartmann extraction of the six planes from
  proj*view (src/core/game.c:298) plus a conservative AABB test
  (src/core/game.c:313), used by ocean cells. Limitations: locked in game.c,
  ocean-only — reusable module + Mesh bounds tracked as island1-mc2v.
- **Clipping: yes (desktop).** Per-primitive clipping to the view volume is
  spec-guaranteed in fixed-function GL (all six frustum planes plus the near
  plane) — no manual clipping is needed. Current geometry is near-plane safe:
  ocean cells are 10-unit quads, sky/HUD are screen-space, wake/billboards
  are small. Console backends cannot rely on HW clipping this way
  (render-dc.md).
- **Culling**: backface via GL_CULL_FACE (GL_CCW wound, GL_BACK); frustum
  culling at draw level (ocean only).
- **Fog**: linear, colour = sky horizon (seamless), per-draw on/off,
  start/end from `r_fog()`. Only GL_LINEAR is reachable (island1-pqhi).
- Depth test (GL_LEQUAL), blend, 2D textures (NEAREST, no mips), per-draw
  tint, screen-space ortho unit, batched quads/lines/triangles, reusable
  meshes with 16-bit indices (console-compatible), billboards emitted as
  world-space triangles, 2D text batched (one draw per string), per-pass
  stats with the F3 overlay.

## Gaps (tracked in pellets)

| Gap | Why it matters | Pellet |
| --- | --- | --- |
| Alpha test | cutout textures (canopies, fences, stippled spray); cheaper than blend and needs no transparent sorting; native on PSP (GU_ALPHA_TEST) and DC (punch-through list) | island1-qb9x |
| Dead lit plumbing | RVertex has no normals; `r_mesh` forces lit=0; `setup_lights()` configures lights nothing can enable; decision: remove | island1-agob |
| Frustum not reusable, no Mesh bounds | locked in game.c for ocean only; car/parcel/props never culled; island content needs O(1) per-mesh culling | island1-mc2v |
| Scissor | minimap/splitscreen/HUD panels (island1-n7to); native on PSP, emulated on DC | island1-4t0s |
| tex_env pinned to MODULATE | REPLACE (HUD icons) / ADD (additive glow) unreachable; DC has no GL_ADD equivalent — backend maps it | island1-01nc |
| Fog mode not in frame | only linear reachable; PSP GE is linear-only, DC can do exp via fog tables — document or add mode+density | island1-pqhi |
| Frame caps | 16384 verts / 512 draws; ocean alone is 6400 verts unculled; revisit when island content lands | island1-41ct |

## Not to be added

- **Stencil** — neither Dreamcast nor PSP has a usable stencil buffer; on DC,
  modifier volumes are the idiomatic replacement (shadow/clip volumes).
- **Multitexture** — one texture stage on both consoles; the API is already
  compliant (per-mesh single texture).
- **VBOs / VAOs / shaders / FBOs** — breaks the 1:1 console mapping; client
  arrays + 16-bit indices map directly to both targets' submission models.

## PS1-style presentation

Backend-level, not API gaps (docs/art-direction.md "Presentation",
island1-a440): vertex snap jitter, affine texture warp, low internal render
resolution with NEAREST upscale, dithering. Desktop GL always
perspective-corrects and GL_DITHER is a no-op on modern desktops, so these are
emulated per-backend — see render-dc.md and render-psp.md for what each
console gives natively.

## Console readiness

What already carries over 1:1 to PSP/Dreamcast:

- Plain-data RenderFrame → the backends read it and issue native draws.
- 16-bit indices (GL_UNSIGNED_SHORT) → both targets' submission models.
- Baked vertex colours, no lighting dependency → free on both.
- Linear fog + sky-as-fog → GE fog (PSP) and PVR fog tables (DC).
- Quads → expanded per-backend (GE has no quad primitive; PVR has no quads
  either).
- Screen-space draws → the [0,1] ortho maps to a simple GE/ortho setup;
  `r_rect_2d` maps to GU_SPRITES.
- Batched text/wake → keeps display lists and TA lists small.
