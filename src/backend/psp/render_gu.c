#include "render_gu.h"

#include <string.h>

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include "mesh.h"     /* core Mesh type */
#include "render.h"   /* core RenderAPI: r_frame(), RenderFrame, RDraw, RVertex */
#include "texture_gu.h"

/*
 * PSP GE backend. The recorded RenderFrame is plain data, so this file is only
 * a translation layer: matrices go straight into sceGuSetMatrix (cglm and
 * sceGumPerspective share the same column-major layout), vertices are repacked
 * into the GU vertex formats, and quad/line/triangle draws map 1:1 onto GE
 * primitives. There is no quad primitive on the GE, so R_QUADS is expanded.
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

/* Attribute order must match the GU VTYPE bits: texture, colour, normal, pos. */
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
 * Near-plane clipping. The GE drops a whole triangle that straddles the eye
 * (it projects past the guard band instead of being clipped), so world-space
 * triangles are clipped on the CPU before submission.
 *
 * Clip-space convention is GL's: a perspective projection puts the eye plane at
 * z + w = 0, so "inside" is z + w >= 0. Testing z alone would throw away the
 * entire near half of the frustum (z/w in [-1,0)).
 */
#define PSP_CLIP_EPS 1e-4f

typedef struct {
    float x, y, z, w; /* clip space while clipping, object space otherwise */
    float r, g, b, a; /* tint-applied vertex colour, packed on emit */
} ClipVert;

/* Column-major (GL/cglm) 4x4 times a vector. */
static inline void mat4_mul_vec4(const ScePspFMatrix4* m, float x, float y,
                                float z, float w, float* ox, float* oy,
                                float* oz, float* ow) {
    const float* a = (const float*)m;
    *ox = a[0] * x + a[4] * y + a[8] * z + a[12] * w;
    *oy = a[1] * x + a[5] * y + a[9] * z + a[13] * w;
    *oz = a[2] * x + a[6] * y + a[10] * z + a[14] * w;
    *ow = a[3] * x + a[7] * y + a[11] * z + a[15] * w;
}

static inline void clip_vert_lerp(const ClipVert* a, const ClipVert* b, float t,
                                 ClipVert* out) {
    out->x = a->x + (b->x - a->x) * t;
    out->y = a->y + (b->y - a->y) * t;
    out->z = a->z + (b->z - a->z) * t;
    out->w = a->w + (b->w - a->w) * t;
    out->r = a->r + (b->r - a->r) * t;
    out->g = a->g + (b->g - a->g) * t;
    out->b = a->b + (b->b - a->b) * t;
    out->a = a->a + (b->a - a->a) * t;
}

/* Sutherland-Hodgman against the near plane. One plane turns a triangle into at
 * most four vertices, so out[] never needs more room than that. */
static int clip_tri_near(const ClipVert in[3], ClipVert out[6]) {
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const ClipVert* p = &in[i];
        const ClipVert* q = &in[(i + 1) % 3];
        float dp = p->z + p->w;
        float dq = q->z + q->w;
        int pin = dp >= -PSP_CLIP_EPS;
        int qin = dq >= -PSP_CLIP_EPS;
        if (pin)
            out[n++] = *p;
        if (pin != qin)
            clip_vert_lerp(p, q, dp / (dp - dq), &out[n++]);
    }
    return n;
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
static void mat4_mul4(const ScePspFMatrix4* a, const ScePspFMatrix4* b,
                     ScePspFMatrix4* out) {
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

/* out = m^-1, column-major. Returns 0 if m is singular. */
static int mat4_inv(const ScePspFMatrix4* m, ScePspFMatrix4* out) {
    const float* a = (const float*)m;
    float b00 = a[0] * a[5] - a[4] * a[1];
    float b01 = a[0] * a[6] - a[4] * a[2];
    float b02 = a[0] * a[7] - a[4] * a[3];
    float b03 = a[1] * a[6] - a[5] * a[2];
    float b04 = a[1] * a[7] - a[5] * a[3];
    float b05 = a[2] * a[7] - a[6] * a[3];
    float b06 = a[8] * a[13] - a[12] * a[9];
    float b07 = a[8] * a[14] - a[12] * a[10];
    float b08 = a[8] * a[15] - a[12] * a[11];
    float b09 = a[9] * a[14] - a[13] * a[10];
    float b10 = a[9] * a[15] - a[13] * a[11];
    float b11 = a[10] * a[15] - a[14] * a[11];
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 +
                b05 * b06;
    if (det == 0.0f)
        return 0;
    det = 1.0f / det;

    float o[16];
    o[0] = (a[5] * b11 - a[6] * b10 + a[7] * b09) * det;
    o[1] = (a[2] * b10 - a[1] * b11 - a[3] * b09) * det;
    o[2] = (a[13] * b05 - a[14] * b04 + a[15] * b03) * det;
    o[3] = (a[10] * b04 - a[9] * b05 - a[11] * b03) * det;
    o[4] = (a[6] * b08 - a[4] * b11 - a[7] * b07) * det;
    o[5] = (a[0] * b11 - a[2] * b08 + a[3] * b07) * det;
    o[6] = (a[14] * b02 - a[12] * b05 - a[15] * b01) * det;
    o[7] = (a[8] * b05 - a[10] * b02 + a[11] * b01) * det;
    o[8] = (a[4] * b10 - a[5] * b08 + a[7] * b06) * det;
    o[9] = (a[1] * b08 - a[0] * b10 - a[3] * b06) * det;
    o[10] = (a[12] * b04 - a[13] * b02 + a[15] * b00) * det;
    o[11] = (a[9] * b02 - a[8] * b04 - a[11] * b00) * det;
    o[12] = (a[5] * b07 - a[4] * b09 - a[6] * b06) * det;
    o[13] = (a[0] * b09 - a[1] * b07 + a[2] * b06) * det;
    o[14] = (a[13] * b01 - a[12] * b03 - a[14] * b00) * det;
    o[15] = (a[8] * b03 - a[9] * b01 + a[10] * b00) * det;
    memcpy(out, o, sizeof(o));
    return 1;
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

    /* The GE has no per-draw lists, so the declared composite class (island1-m4g4)
     * only reaches hardware through depth: an overlay must draw over everything,
     * so it neither tests against nor writes scene depth. A screen-space draw that
     * is *not* an overlay (the sky) is part of the scene and keeps depth testing. */
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
    out->w = 1.0f;
    out->r = c[0];
    out->g = c[1];
    out->b = c[2];
    out->a = c[3];
}

static void emit_v(const ClipVert* v, GuVtx* out, int* n) {
    float c[4] = { v->r, v->g, v->b, v->a };
    out[*n].color = pack_rgba(c);
    out[*n].x = v->x;
    out[*n].y = v->y;
    out[*n].z = v->z;
    (*n)++;
}

/*
 * Near-clip one triangle and append the resulting fan to out. mvp/minv are NULL
 * for draws that need no clipping (screen space is orthographic with w = 1, so
 * it never crosses the plane).
 *
 * Clipped vertices go back through minv before being written: the GE still
 * applies proj * view, and M^-1 lands the clipped clip-space vertex on the
 * object-space point that transforms into it. Keeping the GE's own matrices is
 * what preserves hardware fog, which is derived from view space.
 */
static void emit_tri(const ClipVert obj[3], const ScePspFMatrix4* mvp,
                     const ScePspFMatrix4* minv, GuVtx* out, int* n) {
    ClipVert tmp[6];
    tmp[0] = obj[0];
    tmp[1] = obj[1];
    tmp[2] = obj[2];
    int cn = 3;

    if (mvp) {
        ClipVert clp[3];
        int clipped = 0;
        for (int i = 0; i < 3; i++) {
            mat4_mul_vec4(mvp, obj[i].x, obj[i].y, obj[i].z, obj[i].w,
                          &clp[i].x, &clp[i].y, &clp[i].z, &clp[i].w);
            clp[i].r = obj[i].r;
            clp[i].g = obj[i].g;
            clp[i].b = obj[i].b;
            clp[i].a = obj[i].a;
            if (clp[i].z + clp[i].w < -PSP_CLIP_EPS)
                clipped = 1;
        }
        if (clipped) {
            cn = clip_tri_near(clp, tmp);
            if (cn < 3)
                return;
            for (int i = 0; i < cn; i++) {
                float x, y, z, w;
                mat4_mul_vec4(minv, tmp[i].x, tmp[i].y, tmp[i].z, tmp[i].w,
                              &x, &y, &z, &w);
                if (w == 0.0f)
                    return;
                tmp[i].x = x / w;
                tmp[i].y = y / w;
                tmp[i].z = z / w;
            }
        }
    }

    for (int t = 1; t + 1 < cn; t++) {
        emit_v(&tmp[0], out, n);
        emit_v(&tmp[t], out, n);
        emit_v(&tmp[t + 1], out, n);
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
        /* Lines still copy through: only a triangle that straddles the eye
         * blows past the guard band (see emit_tri). */
        GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)count * sizeof(GuVtx));
        for (int i = 0; i < count; i++) {
            out[i].color = shade(&v[i], tint);
            out[i].x = v[i].x;
            out[i].y = v[i].y;
            out[i].z = v[i].z;
        }
        sceGuDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                       count, NULL, out);
        return;
    }

    ScePspFMatrix4 mvp, minv;
    const ScePspFMatrix4* clip_mvp = NULL;
    const ScePspFMatrix4* clip_minv = NULL;
    if (!screen) {
        mat4_mul4(&s_proj, &s_view, &mvp);
        if (mat4_inv(&mvp, &minv)) {
            clip_mvp = &mvp;
            clip_minv = &minv;
        }
    }

    static const int quad_idx[6] = { 0, 1, 2, 0, 2, 3 };
    static const int tri_idx[3] = { 0, 1, 2 };
    int step = (d->prim == R_QUADS) ? 4 : 3;
    const int* order = (step == 4) ? quad_idx : tri_idx;
    int ntri = (count / step) * (step / 2);
    if (ntri <= 0)
        return;

    /* A near-clipped triangle fans out to at most six vertices. */
    int cap = ntri * (clip_mvp ? 6 : 3);
    GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)cap * sizeof(GuVtx));
    int n = 0;

    for (int q = 0; q + step <= count; q += step) {
        for (int k = 0; k < step / 2; k++) {
            ClipVert tri[3];
            for (int t = 0; t < 3; t++)
                clip_vert_from(&v[q + order[k * 3 + t]], tint, &tri[t]);
            emit_tri(tri, clip_mvp, clip_minv, out, &n);
        }
    }

    if (n > 0) {
        sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                       n, NULL, out);
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

    /* Indices also come from the display list; copy them fresh each draw. */
    unsigned short* indices = (unsigned short*)sceGuGetMemory(
        (unsigned int)m->index_count * sizeof(unsigned short));
    memcpy(indices, m->indices, (size_t)m->index_count * sizeof(unsigned short));

    if (textured) {
        if (m->texture != s_cur_tex) {
            texture_gu_apply(m->texture);
            s_cur_tex = m->texture;
        }
        GuTexVtx* out =
            (GuTexVtx*)sceGuGetMemory((unsigned int)m->vert_count * sizeof(GuTexVtx));
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
                       m->index_count, indices, out);
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
                       m->index_count, indices, out);
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
