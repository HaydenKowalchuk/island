#include "mesh.h"

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
 * Winding is irrelevant since boxes draw double-sided.
 */
static const int BOX_FACE[6][4] = {
    { 4, 5, 7, 6 }, /* +X */
    { 0, 2, 3, 1 }, /* -X */
    { 2, 3, 7, 6 }, /* +Y (top) */
    { 0, 1, 5, 4 }, /* -Y (bottom) */
    { 1, 3, 7, 5 }, /* +Z */
    { 0, 4, 6, 2 }, /* -Z */
};

static const float BOX_SHADE[6] = { 0.78f, 0.78f, 1.00f, 0.45f, 0.88f, 0.88f };

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void mesh_box(const float center[3], const float half[3], const float color[4],
              int fog, int lit) {
    float corner[8][3];

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

    for (int f = 0; f < 6; f++) {
        float shade = BOX_SHADE[f];
        float c[4] = {
            clamp01(color[0] * shade),
            clamp01(color[1] * shade),
            clamp01(color[2] * shade),
            color[3],
        };
        const int* q = BOX_FACE[f];
        mesh_quad(corner[q[0]], corner[q[1]], corner[q[2]], corner[q[3]],
                  c, fog, lit, R_CULL_NONE);
    }
}
