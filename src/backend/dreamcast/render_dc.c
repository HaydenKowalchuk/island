#include "render_dc.h"

#include <dc/pvr.h>
#include <dc/video.h>
#include <string.h>

#include "mesh.h"
#include "render.h"
#include "texture_dc.h"

/*
 * Native KOS PVR (Tile Accelerator) backend. No GLdc, no GL: each frame the
 * recorded RenderFrame is grouped into PVR polygon lists and submitted as
 * screen-space TA vertices (see docs/render-dc.md for the full contract).
 *
 * Conventions, decoded from the KOS modifier_volume_zclip example and
 * mat_trans_single (verified against the local KOS tree):
 *   - Matrices: KOS matrix_t is the transpose of GL column-major; we do all
 *     transforms in plain C from the frame's GL column-major matrices instead.
 *   - TA vertices are screen space: x/y in pixels, y down; UVs normalised.
 *   - TA z is the PVR W-buffer inverse depth znear/w in (0,1] — bigger is
 *     closer, pairing with PVR_DEPTHCMP_GREATER. znear is extracted from the
 *     GL projection each frame (GL: n = proj[14] / (proj[10] - 1)).
 *   - Fog is computed per vertex with GL's linear formula and baked into the
 *     colours, so no PVR fog table is needed (its units vs our W-buffer z are
 *     unverified; tracked in island1-fo7c).
 *
 * TODO(island1-fo7c, on emulator/hardware): verify culling winding, the
 * W-buffer z mapping and the fog look in redream/NullDC; adjust here first if
 * depth acts inverted.
 */

/* Near-plane clip epsilon on clip-space w. */
#define DC_CLIP_EPS 1e-4f

/*
 * One draw's expanded vertex stream, reused across draws. Sized for the
 * recorder's R_MAX_VERTS (island1-41ct); draws that would overflow are
 * truncated below. pvr_vertex_t is 32 bytes (KOS aligns it), so this costs
 * 512 KB of static main RAM — fine on the Dreamcast's 16 MB.
 */
#define DC_SCRATCH_VERTS 16384
static pvr_vertex_t s_scratch[DC_SCRATCH_VERTS];

/* Framebuffer size, cached from the last present. */
static int s_fb_w = 640;
static int s_fb_h = 480;

/* Fog state for the CPU vertex fog. */
static int s_fog_on;
static float s_fog_start;
static float s_fog_end;
static float s_fog_r;
static float s_fog_g;
static float s_fog_b;

/* GL projection's near plane, extracted per frame for the W-buffer z. */
static float s_znear = 0.1f;

void render_init(void) {
  /* pvr_init requires a suitable 3D video mode to be active first. */
  vid_set_mode(DM_640x480, PM_RGB565);

  /*
   * Enable every list we submit: opaque, translucent and punch-through.
   * pvr_init_defaults() leaves the punch-through bins at zero (disabled), and
   * pvr_list_begin() on a disabled list stalls the PVR — the freeze on boot.
   * Bins and vertex buffers come out of the texture memory pool, so only
   * allocate what is needed. Translucent autosort stays on (the PVR sorts
   * translucent draws by depth, so no painter's sorting from us).
   */
  static const pvr_init_params_t pvr_params = {
      .opb_sizes =
          {
              PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_16, PVR_BINSIZE_0,
              PVR_BINSIZE_16,
          },
      .vertex_buf_size = 512 * 1024,
      .autosort_disabled = 0,
      .opb_overflow_count = 3,
      .vbuf_doublebuf_disabled = 0,
  };
  pvr_init(&pvr_params);

  /* Installs the texture loader. main_dc calls render_init before
   * game_init (mirroring the desktop backend), so content can request
   * textures and have them uploaded to PVR VRAM immediately. */
  texture_dc_init();
}

void render_shutdown(void) { texture_dc_shutdown(); }

/*
 * GL linear fog, per vertex: f = (end - dist) / (end - start), colour =
 * f * vertex + (1 - f) * fog — the same formula GL applies per pixel. dist is
 * the eye-space distance, which for the GL projection equals clip-space w.
 */
static float dc_fog_factor(float dist) {
  if (!s_fog_on)
    return 1.0f;
  if (dist <= s_fog_start)
    return 1.0f;
  if (dist >= s_fog_end)
    return 0.0f;
  return (s_fog_end - dist) / (s_fog_end - s_fog_start);
}

/* out = a * b, column-major. */
static void mat_mul(const float *a, const float *b, float *out) {
  for (int c = 0; c < 4; c++) {
    for (int r = 0; r < 4; r++) {
      float s = 0.0f;
      for (int k = 0; k < 4; k++)
        s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
  }
}

/* Clip-space position through the combined matrix (GL column-major). */
typedef struct {
  float x, y, z, w;
} ClipVert;

/* A clipped vertex with its interpolated attributes. */
typedef struct {
  ClipVert pos;
  float r, g, b, a, u, v;
} ClipFull;

static void clip_full_world(const float *m, const RVertex *v, ClipFull *out) {
  float x = v->x, y = v->y, z = v->z;
  out->pos.x = m[0] * x + m[4] * y + m[8] * z + m[12];
  out->pos.y = m[1] * x + m[5] * y + m[9] * z + m[13];
  out->pos.z = m[2] * x + m[6] * y + m[10] * z + m[14];
  out->pos.w = m[3] * x + m[7] * y + m[11] * z + m[15];
  out->r = v->r;
  out->g = v->g;
  out->b = v->b;
  out->a = v->a;
  out->u = v->u;
  out->v = v->v;
}

/* Screen-space draws enter clip space with w = 1 (no clip needed). */
static void clip_full_unit(const RVertex *v, ClipFull *out) {
  out->pos.x = v->x;
  out->pos.y = v->y;
  out->pos.z = 0.0f;
  out->pos.w = 1.0f;
  out->r = v->r;
  out->g = v->g;
  out->b = v->b;
  out->a = v->a;
  out->u = v->u;
  out->v = v->v;
}

static void clip_full_lerp(const ClipFull *p, const ClipFull *q, float t,
                           ClipFull *out) {
  out->pos.x = p->pos.x + (q->pos.x - p->pos.x) * t;
  out->pos.y = p->pos.y + (q->pos.y - p->pos.y) * t;
  out->pos.z = p->pos.z + (q->pos.z - p->pos.z) * t;
  out->pos.w = p->pos.w + (q->pos.w - p->pos.w) * t;
  out->r = p->r + (q->r - p->r) * t;
  out->g = p->g + (q->g - p->g) * t;
  out->b = p->b + (q->b - p->b) * t;
  out->a = p->a + (q->a - p->a) * t;
  out->u = p->u + (q->u - p->u) * t;
  out->v = p->v + (q->v - p->v) * t;
}

/*
 * Near-plane clip, Sutherland-Hodgman against w > eps, interpolating colour
 * and UV at the crossings. One plane adds at most one vertex, so 3 in gives
 * at most 4 out. Returns the output count; 0 means fully clipped.
 */
#define DC_CLIP_MAX 5

static int dc_clip_near(ClipFull *poly, int n) {
  ClipFull tmp[DC_CLIP_MAX];
  int m = 0;

  for (int i = 0; i < n; i++) {
    const ClipFull *cur = &poly[i];
    const ClipFull *nxt = &poly[(i + 1) % n];
    float dc = cur->pos.w - DC_CLIP_EPS;
    float dn = nxt->pos.w - DC_CLIP_EPS;

    if (dc >= 0.0f)
      tmp[m++] = *cur;
    if ((dc >= 0.0f) != (dn >= 0.0f)) {
      ClipFull x;
      clip_full_lerp(cur, nxt, dc / (dc - dn), &x);
      tmp[m++] = x;
    }
  }

  for (int i = 0; i < m; i++)
    poly[i] = tmp[i];
  return m;
}

/*
 * Project to PVR screen space and pack. z = znear/w — the PVR W-buffer
 * inverse depth in (0,1]; bigger is closer (PVR_DEPTHCMP_GREATER).
 */
static void dc_pack_vert(const ClipFull *v, int fog, const float tint[4],
                         pvr_vertex_t *out, unsigned int flags) {
  float inv_w = 1.0f / v->pos.w;

  out->flags = flags;
  out->x = (v->pos.x * inv_w * 0.5f + 0.5f) * (float)s_fb_w;
  out->y = (1.0f - (v->pos.y * inv_w * 0.5f + 0.5f)) * (float)s_fb_h;
  out->z = s_znear * inv_w;

  float r = v->r * tint[0];
  float g = v->g * tint[1];
  float b = v->b * tint[2];
  if (fog) {
    float f = dc_fog_factor(v->pos.w);
    if (f < 1.0f) {
      r = f * r + (1.0f - f) * s_fog_r;
      g = f * g + (1.0f - f) * s_fog_g;
      b = f * b + (1.0f - f) * s_fog_b;
    }
  }
  out->argb = PVR_PACK_COLOR(v->a * tint[3], r, g, b);
  out->oargb = 0;
  out->u = v->u;
  out->v = v->v;
}

/*
 * Screen-space draws map the [0,1] unit square straight to pixels. z = 0 puts
 * them at the far plane: the sky (opaque, depth-write off) then sorts behind
 * every opaque polygon, and HUD draws (translucent list) render after all
 * opaque anyway. Caveat: translucent draws sort by depth, so far HUD sorts
 * behind near translucent — fine while they never overlap (island1-qb9x's
 * punch-through list will give HUD a proper last-draw home later).
 */
static void dc_pack_vert_2d(const ClipFull *v, const float tint[4],
                            pvr_vertex_t *out, unsigned int flags) {
  out->flags = flags;
  out->x = v->pos.x * (float)s_fb_w;
  out->y = (1.0f - v->pos.y) * (float)s_fb_h;
  out->z = 0.0f;

  out->argb = PVR_PACK_COLOR(v->a * tint[3], v->r * tint[0], v->g * tint[1],
                             v->b * tint[2]);
  out->oargb = 0;
  out->u = v->u;
  out->v = v->v;
}

/*
 * Transform, near-clip and pack one triangle. World-space triangles are
 * clipped against the near plane (the PVR TA produces garbage for vertices
 * behind the eye); screen-space draws skip the clip (w = 1). Emits a triangle
 * LIST — PVR_CMD_VERTEX, PVR_CMD_VERTEX, PVR_CMD_VERTEX_EOL. Returns the
 * number of vertices written (0..6).
 */
static int dc_emit_tri(const float *m, int screen_space, int fog,
                       const float tint[4], const RVertex *a, const RVertex *b,
                       const RVertex *c, pvr_vertex_t *out) {
  ClipFull poly[3];
  int n;

  if (screen_space) {
    clip_full_unit(a, &poly[0]);
    clip_full_unit(b, &poly[1]);
    clip_full_unit(c, &poly[2]);
    n = 3;
  } else {
    clip_full_world(m, a, &poly[0]);
    clip_full_world(m, b, &poly[1]);
    clip_full_world(m, c, &poly[2]);
    n = dc_clip_near(poly, 3);
    if (n < 3)
      return 0;
  }

  int written = 0;
  for (int i = 1; i + 1 < n; i++) {
    if (screen_space) {
      dc_pack_vert_2d(&poly[0], tint, &out[written], PVR_CMD_VERTEX);
      dc_pack_vert_2d(&poly[i], tint, &out[written + 1], PVR_CMD_VERTEX);
      dc_pack_vert_2d(&poly[i + 1], tint, &out[written + 2],
                      PVR_CMD_VERTEX_EOL);
    } else {
      dc_pack_vert(&poly[0], fog, tint, &out[written], PVR_CMD_VERTEX);
      dc_pack_vert(&poly[i], fog, tint, &out[written + 1], PVR_CMD_VERTEX);
      dc_pack_vert(&poly[i + 1], fog, tint, &out[written + 2],
                   PVR_CMD_VERTEX_EOL);
    }
    written += 3;
  }
  return written;
}

/*
 * Which PVR list a draw belongs to. Punch-through (alpha test) comes with
 * island1-qb9x; until then no draw lands there. On the PVR the lists render
 * in fixed order (opaque, translucent, punch-through) and the TA sorts within
 * each list, so the recorder's draw order only picks the list — no painter's
 * sorting is needed from us.
 */
static pvr_list_t dc_list_of(const RDraw *d) {
  if (d->blend)
    return PVR_LIST_TR_POLY;
  return PVR_LIST_OP_POLY;
}

static void dc_submit_draw(const RenderFrame *frame, const RDraw *d,
                           pvr_list_t list) {
  /* Poly header, compiled per draw from the flags. */
  pvr_poly_cxt_t cxt;
  const DcTexture *tex = NULL;
  if (d->textured && d->mesh)
    tex = texture_dc_lookup(d->mesh->texture);

  /* Recorded draws flagged textured but without a mesh have no handle to
   * bind; an untextured header with vertex colour is the same visual as the
   * desktop's white-default MODULATE. */
  if (tex)
    pvr_poly_cxt_txr(&cxt, list, (int)tex->fmt, tex->w, tex->h, tex->vram,
                     PVR_FILTER_NEAREST);
  else
    pvr_poly_cxt_col(&cxt, list);

  cxt.gen.culling =
      (d->cull == R_CULL_BACK) ? PVR_CULLING_CCW : PVR_CULLING_NONE;
  cxt.depth.comparison = PVR_DEPTHCMP_GREATER;
  cxt.depth.write =
      d->depth_write ? PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE;
  cxt.gen.fog_type = PVR_FOG_DISABLE; /* fog is baked into the colours */

  if (list == PVR_LIST_TR_POLY) {
    /* Blend is per-header and opt-in on the PVR (KOS defaults DISABLE). */
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    cxt.blend.src_enable = PVR_BLEND_ENABLE;
    cxt.blend.dst_enable = PVR_BLEND_ENABLE;
  }

  pvr_poly_hdr_t hdr;
  pvr_poly_compile(&hdr, &cxt);
  pvr_prim(&hdr, sizeof(hdr));

  /* Combined matrix proj * view * world (GL column-major). */
  float m[16];
  int screen_space = d->screen_space;
  if (!screen_space) {
    float pv[16];
    mat_mul(frame->proj, frame->view, pv);
    mat_mul(pv, d->world, m);
  }

  int fog = d->fog && frame->fog_enabled && !screen_space;
  const RVertex *verts = frame->verts;
  int written = 0;

  if (d->mesh) {
    const Mesh *mesh = d->mesh;
    for (int i = 0; i + 2 < mesh->index_count; i += 3) {
      if (written + 8 > DC_SCRATCH_VERTS)
        break; /* overflow guard (island1-41ct) */
      written += dc_emit_tri(
          m, screen_space, fog, d->tint, &mesh->verts[mesh->indices[i]],
          &mesh->verts[mesh->indices[i + 1]],
          &mesh->verts[mesh->indices[i + 2]], &s_scratch[written]);
    }
  } else if (d->prim == R_TRIANGLES) {
    for (int i = 0; i + 2 < d->count; i += 3) {
      if (written + 8 > DC_SCRATCH_VERTS)
        break;
      written += dc_emit_tri(m, screen_space, fog, d->tint,
                             &verts[d->first + i], &verts[d->first + i + 1],
                             &verts[d->first + i + 2], &s_scratch[written]);
    }
  } else if (d->prim == R_QUADS) {
    for (int i = 0; i + 3 < d->count; i += 4) {
      if (written + 12 > DC_SCRATCH_VERTS)
        break;
      const RVertex *q = &verts[d->first + i];
      written += dc_emit_tri(m, screen_space, fog, d->tint, &q[0], &q[1], &q[2],
                             &s_scratch[written]);
      written += dc_emit_tri(m, screen_space, fog, d->tint, &q[0], &q[2], &q[3],
                             &s_scratch[written]);
    }
  }
  /* R_LINES: the TA has no line primitive; skipped for now (dev-only
   * draws). Add thin-quad expansion here if lines ever matter on console. */

  if (written > 0)
    pvr_prim(s_scratch, (unsigned int)written * sizeof(pvr_vertex_t));
}

static void dc_submit_list(pvr_list_t list, const RenderFrame *frame) {
  pvr_list_begin(list);
  for (int i = 0; i < frame->draw_count; i++) {
    const RDraw *d = &frame->draws[i];
    if (dc_list_of(d) != list)
      continue;
    dc_submit_draw(frame, d, list);
  }
  pvr_list_finish();
}

void render_present(int fb_w, int fb_h) {
  const RenderFrame *frame = r_frame();

  if (fb_w > 0)
    s_fb_w = fb_w;
  if (fb_h > 0)
    s_fb_h = fb_h;

  s_fog_on = frame->fog_enabled;
  s_fog_start = frame->fog_start;
  s_fog_end = frame->fog_end;
  s_fog_r = frame->fog_color[0];
  s_fog_g = frame->fog_color[1];
  s_fog_b = frame->fog_color[2];

  /*
   * GL projection's near plane: GL column-major has A = proj[10]
   * ((f+n)/(n-f)) and B = proj[14] (2fn/(n-f)); n = B / (A - 1).
   */
  float denom = frame->proj[10] - 1.0f;
  if (denom != 0.0f) {
    float n = frame->proj[14] / denom;
    if (n > 0.0f)
      s_znear = n;
  }

  pvr_set_bg_color(frame->clear[0], frame->clear[1], frame->clear[2]);

  /*
   * No explicit wait before the scene: the KOS examples loop straight into
   * pvr_scene_begin() (it waits for the previous frame internally), and
   * pvr_wait_ready() before any frame has rendered can stall.
   */
  pvr_scene_begin();
  dc_submit_list(PVR_LIST_OP_POLY, frame);
  dc_submit_list(PVR_LIST_TR_POLY, frame);
  dc_submit_list(PVR_LIST_PT_POLY, frame);
  pvr_scene_finish();
}
