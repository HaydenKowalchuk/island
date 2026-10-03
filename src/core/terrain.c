#include "terrain.h"

#include <math.h>
#include <stdlib.h>

#include "mesh.h"
#include "render.h"

/*
 * A single faceted island: a (N+1)^2 heightfield sampled from deterministic
 * value-noise fbm, multiplied by a radial falloff that sinks the rim below the
 * sea so a beach ring forms. Built once into an indexed Mesh and drawn as one
 * terrain pass. No GL here -- Mesh/MeshAPI do all of that.
 */

#define TERRAIN_N 40     /* cells per axis */
#define TERRAIN_STEP 3.0f
#define TERRAIN_CX 0.0f  /* island centre, ahead of the start line */
#define TERRAIN_CZ 78.0f
#define TERRAIN_AMP 20.0f
#define TERRAIN_DROP 34.0f /* how far the rim sinks below sea level */

#define TERRAIN_HALF (TERRAIN_N * TERRAIN_STEP * 0.5f)
#define TERRAIN_VERTS (TERRAIN_N + 1)

static Mesh s_mesh;
static float* s_h; /* (N+1)^2 heightfield, row-major by z then x */
static int s_built = 0;

/* ---- Deterministic value noise --------------------------------------- */

static unsigned int hash_u32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash2f(int x, int z, unsigned int seed) {
    unsigned int h = hash_u32((unsigned int)x * 374761393u +
                              (unsigned int)z * 668265263u + seed);
    return (float)(h & 0xffffffu) / (float)0xffffffu;
}

static float smoothstepf(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static float value_noise(float x, float z, unsigned int seed) {
    int xi = (int)floorf(x);
    int zi = (int)floorf(z);
    float u = smoothstepf(x - (float)xi);
    float v = smoothstepf(z - (float)zi);

    float a = hash2f(xi, zi, seed);
    float b = hash2f(xi + 1, zi, seed);
    float c = hash2f(xi, zi + 1, seed);
    float d = hash2f(xi + 1, zi + 1, seed);

    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}

static float fbm(float x, float z, unsigned int seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < 4; o++) {
        sum += amp * value_noise(x * freq, z * freq, seed + (unsigned int)o * 101u);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

static float terrain_height(float x, float z, unsigned int seed) {
    float dx = x - TERRAIN_CX;
    float dz = z - TERRAIN_CZ;
    float d = sqrtf(dx * dx + dz * dz) / TERRAIN_HALF;
    float r = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
    /* Cubic falloff keeps a broad plateau and a steep, short shore. */
    float fall = 1.0f - r * r * r;

    float n = fbm(x * 0.035f, z * 0.035f, seed);
    float n2 = fbm(x * 0.090f + 11.0f, z * 0.090f - 7.0f, seed + 31u);
    float bump = n * 0.7f + n2 * 0.3f;

    float base = fall * TERRAIN_AMP * (0.25f + 0.85f * bump);
    float shore = (1.0f - fall) * -TERRAIN_DROP;
    return base + shore;
}

static void terrain_color(float h, float x, float z, unsigned int seed, float out[4]) {
    if (h < 0.8f) {
        out[0] = 0.82f;
        out[1] = 0.74f;
        out[2] = 0.52f; /* sand */
    } else if (h < 11.0f) {
        float v = value_noise(x * 0.5f, z * 0.5f, seed + 7u) * 0.12f;
        out[0] = 0.22f + v;
        out[1] = 0.46f + v;
        out[2] = 0.20f + v; /* grass */
    } else {
        out[0] = 0.44f;
        out[1] = 0.42f;
        out[2] = 0.40f; /* rock */
    }
    out[3] = 1.0f;
}

/* ---- Public API ------------------------------------------------------ */

void terrain_init(unsigned int seed) {
    if (seed == 0u)
        seed = 1337u;

    const int V = TERRAIN_VERTS;
    s_h = (float*)malloc(sizeof(float) * (size_t)V * (size_t)V);
    if (!s_h)
        return;

    for (int iz = 0; iz < V; iz++) {
        float z = TERRAIN_CZ - TERRAIN_HALF + (float)iz * TERRAIN_STEP;
        for (int ix = 0; ix < V; ix++) {
            float x = TERRAIN_CX - TERRAIN_HALF + (float)ix * TERRAIN_STEP;
            s_h[iz * V + ix] = terrain_height(x, z, seed);
        }
    }

    mesh_make(&s_mesh);

    for (int iz = 0; iz < TERRAIN_N; iz++) {
        float z0 = TERRAIN_CZ - TERRAIN_HALF + (float)iz * TERRAIN_STEP;
        float z1 = z0 + TERRAIN_STEP;
        for (int ix = 0; ix < TERRAIN_N; ix++) {
            float x0 = TERRAIN_CX - TERRAIN_HALF + (float)ix * TERRAIN_STEP;
            float x1 = x0 + TERRAIN_STEP;

            float h00 = s_h[iz * V + ix];
            float h01 = s_h[(iz + 1) * V + ix];
            float h11 = s_h[(iz + 1) * V + ix + 1];
            float h10 = s_h[iz * V + ix + 1];

            float hmax = fmaxf(fmaxf(h00, h01), fmaxf(h11, h10));
            if (hmax < -1.5f)
                continue; /* deep underwater: the ocean plane hides it anyway */

            float hmin = fminf(fminf(h00, h01), fminf(h11, h10));
            float color[4];
            terrain_color((hmax + hmin) * 0.5f, (x0 + x1) * 0.5f,
                          (z0 + z1) * 0.5f, seed, color);

            /* CCW seen from above, so the +Y normals face the sky. */
            const float a[3] = { x0, h00, z0 };
            const float b[3] = { x0, h01, z1 };
            const float c[3] = { x1, h11, z1 };
            const float d[3] = { x1, h10, z0 };
            mesh_add_quad(&s_mesh, a, b, c, d, color);
        }
    }

    s_built = 1;
}

void terrain_shutdown(void) {
    mesh_destroy(&s_mesh);
    free(s_h);
    s_h = NULL;
    s_built = 0;
}

float terrain_height_at(float x, float z) {
    if (!s_built || !s_h)
        return -1000.0f;

    float fx = (x - (TERRAIN_CX - TERRAIN_HALF)) / TERRAIN_STEP;
    float fz = (z - (TERRAIN_CZ - TERRAIN_HALF)) / TERRAIN_STEP;
    if (fx < 0.0f || fz < 0.0f || fx >= (float)TERRAIN_N || fz >= (float)TERRAIN_N)
        return -1000.0f;

    int ix = (int)fx;
    int iz = (int)fz;
    float tx = fx - (float)ix;
    float tz = fz - (float)iz;

    const int V = TERRAIN_VERTS;
    float h00 = s_h[iz * V + ix];
    float h10 = s_h[iz * V + ix + 1];
    float h01 = s_h[(iz + 1) * V + ix];
    float h11 = s_h[(iz + 1) * V + ix + 1];

    float a = h00 + (h10 - h00) * tx;
    float b = h01 + (h11 - h01) * tx;
    return a + (b - a) * tz;
}

void terrain_emit(void) {
    if (!s_built)
        return;

    static const float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    r_pass(R_PASS_TERRAIN);
    r_mesh(&s_mesh, identity, white);
}
