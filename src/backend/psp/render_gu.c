#include "render_gu.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>
#include <string.h>

#include "mesh.h"   /* core Mesh type */
#include "render.h" /* core RenderAPI: r_frame(), RenderFrame, RDraw, RVertex */
#include "texture_gu.h"

/*
 * PSP GE backend. The recorded RenderFrame is plain data, so this file is only
 * a translation layer: matrices go straight into sceGuSetMatrix (cglm and
 * sceGumPerspective share the same column-major layout), vertices are repacked
 * into the GU vertex formats, and draws map onto GE primitives as triangles or
 * triangle strips. There is no quad primitive on the GE, so R_QUADS becomes a
 * degenerate-stitched triangle strip (strip order (q1,q2,q0,q3) per quad).
 * Nothing is ever submitted as a fan: clipped polygons are decomposed through
 * strip order with strip-parity winding.
 *
 * All per-frame vertex/index data is allocated with sceGuGetMemory, i.e. it
 * lives inside the display list buffer. sceGuFinish flushes that buffer to RAM
 * before the GE runs, which sidesteps the CPU-data-cache coherency problems
 * (garbled vertices on hardware) that external static arrays hit: the GE reads
 * memory through a separate, non-coherent path.
 *
 * Depth uses the reversed convention (far=0, near=65535, GU_GEQUAL) because the
 * GU clears depth to 0 and considers larger values closer.
 */

#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define BUF_WIDTH 512

/* Attribute order must match the GU VTYPE bits: texture, colour, normal, pos.
 */
typedef struct {
  float u, v;
  unsigned int color;
  float x, y, z;
} GuTexVtx;

typedef struct {
  unsigned int color;
  float x, y, z;
} GuVtx;

static unsigned int __attribute__((aligned(16))) s_list[262144];

static ScePspFMatrix4 s_identity;
static ScePspFMatrix4 s_proj_unit;
static ScePspFMatrix4 s_proj;
static ScePspFMatrix4 s_view;
static ScePspFMatrix4 s_model;

/*
 * Clip-space clipping against the GE's whole-primitive discard rules (from
 * the GE docs at ppsspp-site): with T&L on and no clip enable the hardware
 * discards a whole primitive when *any* vertex trips one of
 *
 *   - W <= 0 (vertex behind the camera)
 *   - screen Z outside [0, 65536): the viewport maps z/w = -1 to 65535 and
 *     z/w = +1 to 0, so z + w < 0 (in front of near) or z - w < 0 (past far)
 *     kills the whole triangle
 *   - screen X/Y outside the 0..4096 guard band: viewport pixels are
 *     X_vp = x/w*240 + 2048, Y_vp = y/w*136 + 2048, so the band is
 *     |x/w| <= 8.533 and |y/w| <= 15.059 — a big near-camera tile sprawls
 *     far past that and vanishes entirely, which was the missing ocean tile
 *
 * All four are therefore clipped on the CPU, S-H against six planes
 *
 *   near z + w >= PSP_CLIP_EPS     far  w - z >= PSP_CLIP_EPS
 *   left x + GX*w >= 0            right GX*w - x >= 0
 *   bot  y + GY*w >= 0             top GY*w - y >= 0
 *
 * PSP_CLIP_EPS (0.1) is ~10x the GE's float24 round-off on the clip-matrix
 * row sums (~0.01 absolute at scene coordinates): the hardware re-evaluates
 * z + w in 24-bit float, so a vertex a hair above the plane in float32 can
 * still land below it there and trip the discard — that is why shrinking the
 * old 1e-4 epsilon changed nothing. GX/GY sit inside the guard-band limits
 * for the same reason. w > 0 and screen Z in range then follow from
 * near+far, screen X/Y from GX/GY.
 *
 * The S-H lerp runs in OBJECT space: plane distances come from evaluating
 * mvp per source vertex, and only positions/colours/uvs are interpolated.
 * The GE applies its own matrices to the object-space result, which also
 * preserves hardware fog (derived from view space) — no inverse-matrix
 * round trip. Distances are affine in object space, so later planes reuse
 * the lerped values instead of re-evaluating mvp.
 *
 * Each source triangle's clip code (OR of its vertices' plane bits) tints
 * the emitted polygon so clipped triangles show on screen by which planes
 * they crossed; triangles with code 0 keep their normal colour.
 */
#define PSP_CLIP_EPS 0.1f
#define PSP_CLIP_GX 8.0f
#define PSP_CLIP_GY 14.0f

#define CLIP_NEAR 0x01
#define CLIP_FAR 0x02
#define CLIP_LEFT 0x04
#define CLIP_RIGHT 0x08
#define CLIP_BOT 0x10
#define CLIP_TOP 0x20

/* A triangle clipped against up to six planes has at most nine vertices;
 * staged emission batches this many at a time (see CLIP_STAGE below). */
#define CLIP_MAX_POLY 9
#define CLIP_STAGE 150

typedef struct {
  float x, y, z;    /* object space: the only space S-H lerps through */
  float r, g, b, a; /* tint-applied vertex colour, packed on emit */
  float u, v;       /* texture coords (mesh path) */
} ClipVert;

/* Column-major (GL/cglm) 4x4 times a vector. */
static inline void mat4_mul_vec4(const ScePspFMatrix4* m, float x, float y, float z, float w, float* ox, float* oy, float* oz, float* ow) {
  const float* a = (const float*)m;
  *ox = a[0] * x + a[4] * y + a[8] * z + a[12] * w;
  *oy = a[1] * x + a[5] * y + a[9] * z + a[13] * w;
  *oz = a[2] * x + a[6] * y + a[10] * z + a[14] * w;
  *ow = a[3] * x + a[7] * y + a[11] * z + a[15] * w;
}

static inline void clip_vert_lerp(const ClipVert* a, const ClipVert* b, float t, ClipVert* out) {
  out->x = a->x + (b->x - a->x) * t;
  out->y = a->y + (b->y - a->y) * t;
  out->z = a->z + (b->z - a->z) * t;
  out->r = a->r + (b->r - a->r) * t;
  out->g = a->g + (b->g - a->g) * t;
  out->b = a->b + (b->b - a->b) * t;
  out->a = a->a + (b->a - a->a) * t;
  out->u = a->u + (b->u - a->u) * t;
  out->v = a->v + (b->v - a->v) * t;
}

/* Clip-space plane distances of an object-space point; >= 0 = inside. */
static void clip_eval(const ScePspFMatrix4* mvp, float x, float y, float z, float d[6]) {
  float cx, cy, cz, cw;
  mat4_mul_vec4(mvp, x, y, z, 1.0f, &cx, &cy, &cz, &cw);
  d[0] = cz + cw - PSP_CLIP_EPS; /* CLIP_NEAR */
  d[1] = cw - cz - PSP_CLIP_EPS; /* CLIP_FAR  */
  d[2] = cx + PSP_CLIP_GX * cw;  /* CLIP_LEFT */
  d[3] = PSP_CLIP_GX * cw - cx;  /* CLIP_RIGHT */
  d[4] = cy + PSP_CLIP_GY * cw;  /* CLIP_BOT  */
  d[5] = PSP_CLIP_GY * cw - cy;  /* CLIP_TOP  */
}

/*
 * One Sutherland-Hodgman pass against plane p (index into d[6]). Vertex
 * colours/positions lerp through clip_vert_lerp; the six distances ride
 * along, lerped with the same t (they are affine in object space, so this
 * is exact) and let later planes keep classifying without re-evaluating
 * mvp. One pass turns an n-gon into at most n+1 vertices.
 */
static int clip_pass(const ClipVert* vi, const float di[][6], int n, int p, ClipVert* vo, float dop[][6]) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    float dp = di[i][p];
    float dq = di[j][p];
    int pin = dp >= 0.0f;
    if (pin) {
      vo[m] = vi[i];
      for (int k = 0; k < 6; k++)
        dop[m][k] = di[i][k];
      m++;
    }
    if (pin != (dq >= 0.0f)) {
      float t = dp / (dp - dq);
      clip_vert_lerp(&vi[i], &vi[j], t, &vo[m]);
      for (int k = 0; k < 6; k++)
        dop[m][k] = di[i][k] + (di[j][k] - di[i][k]) * t;
      m++;
    }
  }
  return m;
}

static float clip_shade8(unsigned h) {
  unsigned v = h & 255u;
  if (v < 72u)
    v += 72u;
  return (float)v * (1.0f / 255.0f);
}

/* Tint a clipped polygon by its clip code (odd multipliers mod 256 keep
 * distinct codes on distinct colours, floored bright enough to see). */
static void clip_code_tint(int code, ClipVert* v, int n) {
  if (!code)
    return;
  unsigned h = (unsigned)code;
  float r = clip_shade8(h * 73u);
  float g = clip_shade8(h * 149u);
  float b = clip_shade8(h * 211u);
  for (int i = 0; i < n; i++) {
    v[i].r = r;
    v[i].g = g;
    v[i].b = b;
    v[i].a = 1.0f;
  }
}

/* -1 means "unknown" so the first draw installs every piece of GE state. */
static int s_mode = -1; /* 0 = world, 1 = screen-space */
static int s_tex_on = -1;
static int s_cur_tex = -1;
static int s_fog = -1;
static int s_blend = -1;
static int s_dtest = -1;
static int s_dwrite = -1;
static int s_cull = -1;

static unsigned int pack_rgba(const float c[4]) {
  float v[4];
  for (int i = 0; i < 4; i++) {
    v[i] = c[i];
    if (v[i] < 0.0f)
      v[i] = 0.0f;
    if (v[i] > 1.0f)
      v[i] = 1.0f;
  }
  unsigned int r = (unsigned int)(v[0] * 255.0f + 0.5f);
  unsigned int g = (unsigned int)(v[1] * 255.0f + 0.5f);
  unsigned int b = (unsigned int)(v[2] * 255.0f + 0.5f);
  unsigned int a = (unsigned int)(v[3] * 255.0f + 0.5f);
  return (a << 24) | (b << 16) | (g << 8) | r;
}

static void shade4(const RVertex* v, const float tint[4], float out[4]) {
  out[0] = v->r * tint[0];
  out[1] = v->g * tint[1];
  out[2] = v->b * tint[2];
  out[3] = v->a * tint[3];
}

static unsigned int shade(const RVertex* v, const float tint[4]) {
  float c[4];
  shade4(v, tint, c);
  return pack_rgba(c);
}

static void set_tex(int on) {
  if (on == s_tex_on)
    return;
  if (on)
    sceGuEnable(GU_TEXTURE_2D);
  else
    sceGuDisable(GU_TEXTURE_2D);
  s_tex_on = on;
}

static void set_fog(int on) {
  if (on == s_fog)
    return;
  if (on)
    sceGuEnable(GU_FOG);
  else
    sceGuDisable(GU_FOG);
  s_fog = on;
}

static void set_blend(int on) {
  if (on == s_blend)
    return;
  if (on) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  } else {
    sceGuDisable(GU_BLEND);
  }
  s_blend = on;
}

static void set_dtest(int on) {
  if (on == s_dtest)
    return;
  if (on)
    sceGuEnable(GU_DEPTH_TEST);
  else
    sceGuDisable(GU_DEPTH_TEST);
  s_dtest = on;
}

/* GU depth mask is inverted: non-zero disables writing. */
static void set_dwrite(int on) {
  if (on == s_dwrite)
    return;
  sceGuDepthMask(on ? 0 : 0xffff);
  s_dwrite = on;
}

static void set_cull(int on) {
  if (on == s_cull)
    return;
  if (on)
    sceGuEnable(GU_CULL_FACE);
  else
    sceGuDisable(GU_CULL_FACE);
  s_cull = on;
}

/* out = a * b, column-major. out must not alias a or b. */
static void mat4_mul4(const ScePspFMatrix4* a, const ScePspFMatrix4* b, ScePspFMatrix4* out) {
  const float* A = (const float*)a;
  const float* B = (const float*)b;
  float o[16];
  for (int c = 0; c < 4; c++) {
    for (int r = 0; r < 4; r++) {
      o[c * 4 + r] = A[r] * B[c * 4] + A[4 + r] * B[c * 4 + 1] +
                     A[8 + r] * B[c * 4 + 2] + A[12 + r] * B[c * 4 + 3];
    }
  }
  memcpy(out, o, sizeof(o));
}

static void use_world(const RenderFrame* f) {
  if (s_mode == 0)
    return;
  memcpy(&s_proj, f->proj, sizeof(s_proj));
  memcpy(&s_view, f->view, sizeof(s_view));
  sceGuSetMatrix(GU_PROJECTION, &s_proj);
  sceGuSetMatrix(GU_VIEW, &s_view);
  s_mode = 0;
}

static void use_screen(void) {
  if (s_mode == 1)
    return;
  sceGuSetMatrix(GU_PROJECTION, &s_proj_unit);
  sceGuSetMatrix(GU_VIEW, &s_identity);
  s_mode = 1;
}

static void apply_state(const RenderFrame* f, const RDraw* d, int textured, int screen) {
  use_world(f);
  if (screen)
    use_screen();

  /* The GE has no per-draw lists, so the declared composite class
   * (island1-m4g4) only reaches hardware through depth: an overlay must draw
   * over everything, so it neither tests against nor writes scene depth. A
   * screen-space draw that is *not* an overlay (the sky) is part of the scene
   * and keeps depth testing. */
  int overlay = (d->composite == R_COMPOSITE_OVERLAY);

  set_fog(d->fog && f->fog_enabled);
  set_blend(d->blend);
  set_dtest(!overlay);
  set_dwrite(overlay ? 0 : d->depth_write);
  set_cull(!screen && d->cull == R_CULL_BACK);
  set_tex(textured);
}

static void clip_vert_from(const RVertex* s, const float tint[4], ClipVert* out) {
  float c[4];
  shade4(s, tint, c);
  out->x = s->x;
  out->y = s->y;
  out->z = s->z;
  out->r = c[0];
  out->g = c[1];
  out->b = c[2];
  out->a = c[3];
  out->u = s->u;
  out->v = s->v;
}

static void emit_v(const ClipVert* v, GuVtx* out, int* n) {
  float c[4] = {v->r, v->g, v->b, v->a};
  out[*n].color = pack_rgba(c);
  out[*n].x = v->x;
  out[*n].y = v->y;
  out[*n].z = v->z;
  (*n)++;
}

static void emit_v_tex(const ClipVert* v, GuTexVtx* out, int* n) {
  float c[4] = {v->r, v->g, v->b, v->a};
  out[*n].u = v->u;
  out[*n].v = v->v;
  out[*n].color = pack_rgba(c);
  out[*n].x = v->x;
  out[*n].y = v->y;
  out[*n].z = v->z;
  (*n)++;
}

static void emit_rv(const RVertex* s, const float* tint, GuVtx* out, int* n) {
  out[*n].color = shade(s, tint);
  out[*n].x = s->x;
  out[*n].y = s->y;
  out[*n].z = s->z;
  (*n)++;
}

/*
 * Classify and clip one triangle. mvp is NULL for draws that need no clipping
 * (screen space is orthographic with w = 1, it never leaves the band). An
 * all-inside triangle passes through untouched with code 0; anything crossing
 * a plane runs S-H against the planes its code names. Returns the resulting
 * convex polygon's vertex count (0 = fully outside one plane, same as the
 * GE's whole-primitive cull) and hands back the clip code for the tint.
 */
static int clip_obj_tri(const ClipVert obj[3], const ScePspFMatrix4* mvp, ClipVert out[CLIP_MAX_POLY], int* code_out) {
  *code_out = 0;
  if (!mvp) {
    out[0] = obj[0];
    out[1] = obj[1];
    out[2] = obj[2];
    return 3;
  }
  float d[3][6];
  int code = 0;
  for (int i = 0; i < 3; i++) {
    clip_eval(mvp, obj[i].x, obj[i].y, obj[i].z, d[i]);
    for (int p = 0; p < 6; p++)
      if (d[i][p] < 0.0f)
        code |= 1 << p;
  }
  *code_out = code;
  if (!code) {
    out[0] = obj[0];
    out[1] = obj[1];
    out[2] = obj[2];
    return 3;
  }

  ClipVert va[CLIP_MAX_POLY], vb[CLIP_MAX_POLY];
  float da[CLIP_MAX_POLY][6], db[CLIP_MAX_POLY][6];
  for (int i = 0; i < 3; i++) {
    va[i] = obj[i];
    memcpy(da[i], d[i], sizeof(d[i]));
  }
  ClipVert *src = va, *dst = vb;
  float (*srcd)[6] = da, (*dstd)[6] = db;
  int n = 3;
  for (int p = 0; p < 6 && n >= 3; p++) {
    if (!(code & (1 << p)))
      continue;
    int m = clip_pass(src, srcd, n, p, dst, dstd);
    ClipVert* tv = src;
    src = dst;
    dst = tv;
    float (*td)[6] = srcd;
    srcd = dstd;
    dstd = td;
    n = m;
  }
  if (n < 3)
    return 0;
  memcpy(out, src, (size_t)n * sizeof(ClipVert));
  return n;
}

/*
 * Order a clipped polygon (boundary order from S-H) the way a triangle strip
 * would walk it: v0, v1, then alternately from the back and the front —
 * (p0,p1,p3,p2) for a quad, (p0,p1,p4,p2,p3) for a pentagon, and so on. The
 * strip's own triangles (even index: i,i+1,i+2; odd: i+1,i,i+2) reproduce
 * the polygon's winding with no shared hub vertex.
 */
static int poly_strip_order(const ClipVert* p, int n, ClipVert s[CLIP_MAX_POLY]) {
  int ns = 0;
  s[ns++] = p[0];
  s[ns++] = p[1];
  int lo = 2, hi = n - 1;
  while (lo <= hi) {
    s[ns++] = p[hi--];
    if (lo <= hi)
      s[ns++] = p[lo++];
  }
  return ns;
}

/* Clipped polygon -> plain triangle list (strip order, strip-parity winding).
 */
static void emit_poly_tris(const ClipVert* poly, int n, GuVtx* out, int* idx) {
  ClipVert s[CLIP_MAX_POLY];
  int ns = poly_strip_order(poly, n, s);
  for (int i = 0; i + 2 < ns; i++) {
    const ClipVert* a = (i & 1) ? &s[i + 1] : &s[i];
    const ClipVert* b = (i & 1) ? &s[i] : &s[i + 1];
    const ClipVert* c = &s[i + 2];
    emit_v(a, out, idx);
    emit_v(b, out, idx);
    emit_v(c, out, idx);
  }
}

static void emit_poly_tris_tex(const ClipVert* poly, int n, GuTexVtx* out, int* idx) {
  ClipVert s[CLIP_MAX_POLY];
  int ns = poly_strip_order(poly, n, s);
  for (int i = 0; i + 2 < ns; i++) {
    const ClipVert* a = (i & 1) ? &s[i + 1] : &s[i];
    const ClipVert* b = (i & 1) ? &s[i] : &s[i + 1];
    const ClipVert* c = &s[i + 2];
    emit_v_tex(a, out, idx);
    emit_v_tex(b, out, idx);
    emit_v_tex(c, out, idx);
  }
}

static void present_recorded(const RenderFrame* f, const RDraw* d, int screen) {
  const RVertex* v = &f->verts[d->first];
  const float* tint = d->tint;
  int count = d->count;
  if (count <= 0)
    return;

  /* No recorded draw carries a texture handle, so all are untextured. */
  apply_state(f, d, 0, screen);
  sceGuSetMatrix(GU_MODEL, &s_identity);

  if (d->prim == R_LINES) {
    /* Lines copy through: only triangles near the eye need the clip path
     * (see the near-plane clip comment). */
    GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)count * sizeof(GuVtx));
    for (int i = 0; i < count; i++) {
      out[i].color = shade(&v[i], tint);
      out[i].x = v[i].x;
      out[i].y = v[i].y;
      out[i].z = v[i].z;
    }
    sceGuDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, count, NULL, out);
    return;
  }

  ScePspFMatrix4 mvp;
  const ScePspFMatrix4* clip_mvp = NULL;
  if (!screen) {
    mat4_mul4(&s_proj, &s_view, &mvp);
    clip_mvp = &mvp;
  }
  int n = 0;

  static const int quad_idx[6] = {0, 1, 2, 0, 2, 3};
  static const int tri_idx[3] = {0, 1, 2};
  int step = (d->prim == R_QUADS) ? 4 : 3;
  int nseg = count / step;
  if (nseg <= 0)
    return;

  /* Classify the whole draw once: when no vertex crosses any clip plane the
   * segments go out raw, with no per-vertex transform at all. */
  int needs_clip = 0;
  if (clip_mvp) {
    for (int i = 0; i < count; i++) {
      float d[6];
      clip_eval(clip_mvp, v[i].x, v[i].y, v[i].z, d);
      for (int p = 0; p < 6; p++) {
        if (d[p] < 0.0f) {
          needs_clip = 1;
          break;
        }
      }
      if (needs_clip)
        break;
    }
  }

  if (!needs_clip) {
    if (step == 4) {
      /* Quads -> one triangle strip: strip order (q1,q2,q0,q3) per quad
       * reproduces the source tris (q0,q1,q2)+(q0,q2,q3) under strip parity
       * (even: i,i+1,i+2; odd: i+1,i,i+2), so winding matches the list path
       * for any projected quad. Consecutive quads join through degenerate
       * vertices so the next quad's first triangle lands on an even index. */
      int cap = count + 3 * (nseg + 2);
      GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)cap * sizeof(GuVtx));
      for (int q = 0; q + 4 <= count; q += 4) {
        const RVertex* Q = &v[q];
        if (n > 0) {
          int L = n;
          /* Y = the previous quad's last strip vertex (its q3). */
          emit_rv(&v[q - 1], tint, out, &n);
          if (L & 1)
            emit_rv(&v[q - 1], tint, out, &n);
          emit_rv(&Q[1], tint, out, &n);
        }
        emit_rv(&Q[1], tint, out, &n);
        emit_rv(&Q[2], tint, out, &n);
        emit_rv(&Q[0], tint, out, &n);
        emit_rv(&Q[3], tint, out, &n);
      }
      if (n > 0) {
        sceGuDrawArray(GU_TRIANGLE_STRIP,
                       GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                       n,
                       NULL,
                       out);
      }
      return;
    }
    GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)count * sizeof(GuVtx));
    for (int i = 0; i < count; i++)
      emit_rv(&v[i], tint, out, &n);
    sceGuDrawArray(GU_TRIANGLES,
                   GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   n,
                   NULL,
                   out);
    return;
  }

  /* Something crosses a plane: expand to a plain triangle list, clipping per
   * triangle — a strip cannot survive polygons changing shape. Output is
   * emitted straight into sceGuGetMemory chunks (never the stack: the GE
   * reads display-list RAM later, so the data has to live there), sized so
   * the list only reserves what is actually written — a worst-case
   * ntri * 21 allocation could overrun it. */
  const int* order = (step == 4) ? quad_idx : tri_idx;
  GuVtx* out = NULL;
  int capn = 0;
  n = 0;
  for (int q = 0; q + step <= count; q += step) {
    for (int k = 0; k < step / 2; k++) {
      ClipVert tri[3], poly[CLIP_MAX_POLY];
      int code;
      for (int t = 0; t < 3; t++)
        clip_vert_from(&v[q + order[k * 3 + t]], tint, &tri[t]);
      int cn = clip_obj_tri(tri, clip_mvp, poly, &code);
      if (cn >= 3) {
        // clip_code_tint(code, poly, cn);
        if (n + (cn - 2) * 3 > capn) {
          if (n > 0)
            sceGuDrawArray(GU_TRIANGLES,
                           GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                           n,
                           NULL,
                           out);
          out = (GuVtx*)sceGuGetMemory(CLIP_STAGE * sizeof(GuVtx));
          capn = CLIP_STAGE;
          n = 0;
        }
        emit_poly_tris(poly, cn, out, &n);
      }
    }
  }
  if (n > 0) {
    sceGuDrawArray(GU_TRIANGLES,
                   GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   n,
                   NULL,
                   out);
  }
}

static void present_mesh(const RenderFrame* f, const RDraw* d) {
  const Mesh* m = d->mesh;
  if (!m || m->index_count <= 0 || m->vert_count <= 0)
    return;

  int textured = (m->texture != 0);
  apply_state(f, d, textured, 0);

  memcpy(&s_model, d->world, sizeof(s_model));
  sceGuSetMatrix(GU_MODEL, &s_model);

  if (textured && m->texture != s_cur_tex) {
    texture_gu_apply(m->texture);
    s_cur_tex = m->texture;
  }

  /* Combined matrix for the near-distance classification: the GE applies
   * proj * view * model itself, so object-space clip lerps stay in mesh
   * space and ride the model matrix like everything else. */
  ScePspFMatrix4 pv, mvp;
  const ScePspFMatrix4* clip_mvp = NULL;
  mat4_mul4(&s_proj, &s_view, &pv);
  mat4_mul4(&pv, &s_model, &mvp);
  clip_mvp = &mvp;

  /*
   * The GE's whole-triangle discard rules (see the clip comment) hit terrain
   * under and beside the camera the same way they hit the ocean. Pre-check
   * every vertex against all six planes: fully-inside meshes keep the
   * indexed submission, anything straddling goes through clip_obj_tri.
   */
  int needs_clip = 0;
  if (clip_mvp) {
    for (int i = 0; i < m->vert_count; i++) {
      const RVertex* s = &m->verts[i];
      float d[6];
      clip_eval(clip_mvp, s->x, s->y, s->z, d);
      for (int p = 0; p < 6; p++) {
        if (d[p] < 0.0f) {
          needs_clip = 1;
          break;
        }
      }
      if (needs_clip)
        break;
    }
  }

  if (!needs_clip) {
    /* Indices also come from the display list; copy them fresh each draw. */
    unsigned short* indices = (unsigned short*)sceGuGetMemory(
        (unsigned int)m->index_count * sizeof(unsigned short));
    memcpy(indices, m->indices, (size_t)m->index_count * sizeof(unsigned short));

    if (textured) {
      GuTexVtx* out = (GuTexVtx*)sceGuGetMemory((unsigned int)m->vert_count *
                                                sizeof(GuTexVtx));
      for (int i = 0; i < m->vert_count; i++) {
        const RVertex* s = &m->verts[i];
        out[i].u = s->u;
        out[i].v = s->v;
        out[i].color = shade(s, d->tint);
        out[i].x = s->x;
        out[i].y = s->y;
        out[i].z = s->z;
      }
      sceGuDrawArray(GU_TRIANGLES,
                     GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                         GU_INDEX_16BIT | GU_TRANSFORM_3D,
                     m->index_count,
                     indices,
                     out);
    } else {
      GuVtx* out =
          (GuVtx*)sceGuGetMemory((unsigned int)m->vert_count * sizeof(GuVtx));
      for (int i = 0; i < m->vert_count; i++) {
        const RVertex* s = &m->verts[i];
        out[i].color = shade(s, d->tint);
        out[i].x = s->x;
        out[i].y = s->y;
        out[i].z = s->z;
      }
      sceGuDrawArray(GU_TRIANGLES,
                     GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT |
                         GU_TRANSFORM_3D,
                     m->index_count,
                     indices,
                     out);
    }
    return;
  }

  int ntri = m->index_count / 3;
  int n = 0;

  if (textured) {
    GuTexVtx* out = NULL;
    int capn = 0;
    for (int i = 0; i < ntri; i++) {
      ClipVert tri[3], poly[CLIP_MAX_POLY];
      int code;
      for (int t = 0; t < 3; t++)
        clip_vert_from(&m->verts[m->indices[i * 3 + t]], d->tint, &tri[t]);
      int cn = clip_obj_tri(tri, clip_mvp, poly, &code);
      if (cn >= 3) {
        // clip_code_tint(code, poly, cn);
        if (n + (cn - 2) * 3 > capn) {
          if (n > 0)
            sceGuDrawArray(GU_TRIANGLES,
                           GU_TEXTURE_32BITF | GU_COLOR_8888 |
                               GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                           n,
                           NULL,
                           out);
          out = (GuTexVtx*)sceGuGetMemory(CLIP_STAGE * sizeof(GuTexVtx));
          capn = CLIP_STAGE;
          n = 0;
        }
        emit_poly_tris_tex(poly, cn, out, &n);
      }
    }
    if (n > 0) {
      sceGuDrawArray(GU_TRIANGLES,
                     GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF |
                         GU_TRANSFORM_3D,
                     n,
                     NULL,
                     out);
    }
  } else {
    GuVtx* out = NULL;
    int capn = 0;
    for (int i = 0; i < ntri; i++) {
      ClipVert tri[3], poly[CLIP_MAX_POLY];
      int code;
      for (int t = 0; t < 3; t++)
        clip_vert_from(&m->verts[m->indices[i * 3 + t]], d->tint, &tri[t]);
      int cn = clip_obj_tri(tri, clip_mvp, poly, &code);
      if (cn >= 3) {
        // clip_code_tint(code, poly, cn);
        if (n + (cn - 2) * 3 > capn) {
          if (n > 0)
            sceGuDrawArray(GU_TRIANGLES,
                           GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                           n,
                           NULL,
                           out);
          out = (GuVtx*)sceGuGetMemory(CLIP_STAGE * sizeof(GuVtx));
          capn = CLIP_STAGE;
          n = 0;
        }
        emit_poly_tris(poly, cn, out, &n);
      }
    }
    if (n > 0) {
      sceGuDrawArray(GU_TRIANGLES,
                     GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                     n,
                     NULL,
                     out);
    }
  }
}

/* GE state is not assumed to survive a finish/start cycle: re-install it. */
static void reset_ge_state(void) {
  s_mode = -1;
  s_tex_on = -1;
  s_cur_tex = -1;
  s_fog = -1;
  s_blend = -1;
  s_dtest = -1;
  s_dwrite = -1;
  s_cull = -1;
}

void render_init(void) {
  sceGuInit();

  void* fbp0 = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_5650);
  void* fbp1 = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_5650);
  void* zbp = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_4444);

  sceGuStart(GU_DIRECT, s_list);
  sceGuDrawBuffer(GU_PSM_5650, fbp0, BUF_WIDTH);
  sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, fbp1, BUF_WIDTH);
  sceGuDepthBuffer(zbp, BUF_WIDTH);
  sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
  sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
  sceGuDepthRange(65535, 0);
  sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuFrontFace(GU_CCW);
  sceGuShadeModel(GU_SMOOTH);
  sceGuEnable(GU_CULL_FACE);
  sceGuEnable(GU_CLIP_PLANES);
  sceGuEnable(GU_DITHER);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_FOG);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

  sceDisplayWaitVblankStart();
  sceGuDisplay(GU_TRUE);

  /* Identity and the [0,1]^2 screen-space ortho (GL-compatible bottom-left). */
  sceGumMatrixMode(GU_PROJECTION);
  sceGumLoadIdentity();
  sceGumStoreMatrix(&s_identity);
  sceGumOrtho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
  sceGumStoreMatrix(&s_proj_unit);

  reset_ge_state();

  texture_gu_init();
}

void render_shutdown(void) {
  texture_gu_shutdown();
  sceGuTerm();
}

void render_present(int fb_w, int fb_h) {
  const RenderFrame* f = r_frame();
  (void)fb_w;
  (void)fb_h;

  reset_ge_state();

  sceGuStart(GU_DIRECT, s_list);

  sceGuClearColor(pack_rgba(f->clear));
  sceGuClearDepth(0);
  sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
  sceGuFog(f->fog_start, f->fog_end, pack_rgba(f->fog_color));

  for (int i = 0; i < f->draw_count; i++) {
    const RDraw* d = &f->draws[i];
    if (d->mesh) {
      present_mesh(f, d);
    } else {
      present_recorded(f, d, d->screen_space);
    }
  }

  /* Terminates the list and flushes it (including the sceGuGetMemory vertex
   * and index data) so the GE reads coherent RAM. */
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);

  sceDisplayWaitVblankStart();
  sceGuSwapBuffers();
}
