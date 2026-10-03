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

static unsigned int shade(const RVertex* v, const float tint[4]) {
    float c[4] = { v->r * tint[0], v->g * tint[1], v->b * tint[2], v->a * tint[3] };
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

    set_fog(d->fog && f->fog_enabled);
    set_blend(d->blend);
    set_dtest(!screen);
    set_dwrite(screen ? 0 : d->depth_write);
    set_cull(!screen && d->cull == R_CULL_BACK);
    set_tex(textured);
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

    /* R_QUADS -> 2 triangles each; R_TRIANGLES passes through. */
    int out_count = (d->prim == R_QUADS) ? (count / 4) * 6 : count;
    if (out_count <= 0)
        return;

    GuVtx* out = (GuVtx*)sceGuGetMemory((unsigned int)out_count * sizeof(GuVtx));
    int n = 0;

    if (d->prim == R_QUADS) {
        static const int order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int q = 0; q + 3 < count; q += 4) {
            for (int k = 0; k < 6; k++) {
                const RVertex* s = &v[q + order[k]];
                out[n].color = shade(s, tint);
                out[n].x = s->x;
                out[n].y = s->y;
                out[n].z = s->z;
                n++;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            out[n].color = shade(&v[i], tint);
            out[n].x = v[i].x;
            out[n].y = v[i].y;
            out[n].z = v[i].z;
            n++;
        }
    }

    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   n, NULL, out);
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

    s_mode = -1;
    s_tex_on = -1;
    s_cur_tex = -1;
    s_fog = -1;
    s_blend = -1;
    s_dtest = -1;
    s_dwrite = -1;
    s_cull = -1;

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

    /* Do not assume GE state survives a finish/start cycle: re-install it. */
    s_mode = -1;
    s_tex_on = -1;
    s_cur_tex = -1;
    s_fog = -1;
    s_blend = -1;
    s_dtest = -1;
    s_dwrite = -1;
    s_cull = -1;

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
