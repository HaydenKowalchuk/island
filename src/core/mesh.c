#include "mesh.h"

#include <stdlib.h>

/* ---- Immediate (per-frame) helpers ----------------------------------- */

void mesh_quad(const float a[3], const float b[3], const float c[3], const float d[3],
               const float color[4], int fog, int lit, RCull cull) {
    int first = r_vertex(a[0], a[1], a[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(b[0], b[1], b[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(c[0], c[1], c[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);
    r_vertex(d[0], d[1], d[2], color[0], color[1], color[2], color[3], 0.0f, 0.0f);

    RDraw draw = { 0 };
    draw.prim = R_QUADS;
    draw.first = first;
    draw.count = 4;
    draw.fog = fog;
    draw.lit = lit;
    draw.cull = cull;
    draw.depth_write = 1;
    draw.tint[0] = draw.tint[1] = draw.tint[2] = draw.tint[3] = 1.0f;
    r_draw(&draw);
}

/*
 * Corner numbering: bit2 = x, bit1 = y, bit0 = z (0 = negative side).
 * Faces are wound CCW as seen from outside so back-face culling works.
 */
static const int BOX_FACE[6][4] = {
    { 4, 6, 7, 5 }, /* +X */
    { 0, 1, 3, 2 }, /* -X */
    { 2, 3, 7, 6 }, /* +Y (top) */
    { 0, 4, 5, 1 }, /* -Y (bottom) */
    { 1, 5, 7, 3 }, /* +Z */
    { 0, 2, 6, 4 }, /* -Z */
};

static const float BOX_SHADE[6] = { 0.78f, 0.78f, 1.00f, 0.45f, 0.88f, 0.88f };

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static void box_corners(const float center[3], const float half[3], float corner[8][3]) {
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            for (int z = 0; z < 2; z++) {
                int i = x * 4 + y * 2 + z;
                corner[i][0] = center[0] + (x ? half[0] : -half[0]);
                corner[i][1] = center[1] + (y ? half[1] : -half[1]);
                corner[i][2] = center[2] + (z ? half[2] : -half[2]);
            }
        }
    }
}

static void shaded(const float color[4], float shade, float out[4]) {
    out[0] = clamp01(color[0] * shade);
    out[1] = clamp01(color[1] * shade);
    out[2] = clamp01(color[2] * shade);
    out[3] = color[3];
}

void mesh_box(const float center[3], const float half[3], const float color[4],
              int fog, int lit) {
    float corner[8][3];
    box_corners(center, half, corner);

    for (int f = 0; f < 6; f++) {
        float c[4];
        shaded(color, BOX_SHADE[f], c);
        const int* q = BOX_FACE[f];
        mesh_quad(corner[q[0]], corner[q[1]], corner[q[2]], corner[q[3]],
                  c, fog, lit, R_CULL_NONE);
    }
}

/* ---- Reusable meshes -------------------------------------------------- */

void mesh_make(Mesh* m) {
    m->verts = NULL;
    m->vert_count = 0;
    m->indices = NULL;
    m->index_count = 0;
    m->texture = 0;
}

void mesh_destroy(Mesh* m) {
    free(m->verts);
    free(m->indices);
    mesh_make(m);
}

void mesh_add_quad(Mesh* m, const float a[3], const float b[3], const float c[3],
                   const float d[3], const float color[4]) {
    const float* pts[4] = { a, b, c, d };

    RVertex* verts = realloc(m->verts, (size_t)(m->vert_count + 4) * sizeof(RVertex));
    if (!verts)
        return;
    m->verts = verts;

    unsigned short* idx = realloc(m->indices,
                                  (size_t)(m->index_count + 6) * sizeof(unsigned short));
    if (!idx)
        return;
    m->indices = idx;

    /* Quad UVs run (0,0)->(1,0)->(1,1)->(0,1); textured meshes see one tile per
     * face, untextured ones ignore UVs entirely. */
    static const float QUAD_UV[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f },
                                         { 1.0f, 1.0f }, { 0.0f, 1.0f } };

    unsigned short base = (unsigned short)m->vert_count;
    for (int i = 0; i < 4; i++) {
        RVertex* v = &m->verts[m->vert_count++];
        v->x = pts[i][0]; v->y = pts[i][1]; v->z = pts[i][2];
        v->r = color[0]; v->g = color[1]; v->b = color[2]; v->a = color[3];
        v->u = QUAD_UV[i][0]; v->v = QUAD_UV[i][1];
    }

    m->indices[m->index_count++] = base;
    m->indices[m->index_count++] = base + 1;
    m->indices[m->index_count++] = base + 2;
    m->indices[m->index_count++] = base;
    m->indices[m->index_count++] = base + 2;
    m->indices[m->index_count++] = base + 3;
}

void mesh_add_box(Mesh* m, const float center[3], const float half[3],
                  const float color[4]) {
    float corner[8][3];
    box_corners(center, half, corner);

    for (int f = 0; f < 6; f++) {
        float c[4];
        shaded(color, BOX_SHADE[f], c);
        const int* q = BOX_FACE[f];
        mesh_add_quad(m, corner[q[0]], corner[q[1]], corner[q[2]], corner[q[3]], c);
    }
}
