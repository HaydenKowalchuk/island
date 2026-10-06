#include "terrain.h"

#include <math.h>
#include <string.h>

#include "frustum.h"
#include "mesh.h"
#include "render.h"

/*
 * A few faceted islands built as radial meshes. Each island is a fan from the
 * summit plus bands between concentric rings, so triangle count follows the
 * circular coast instead of a bounding square (island1-39q6 note: the old grid
 * spent most of its tris on flat underwater corners). One mesh per island so a
 * whole island can be frustum- and fog-culled as a unit (island1-762c). No GL
 * here.
 */

#define TERRAIN_ISLAND_COUNT 4
#define TERRAIN_RINGS 6
#define TERRAIN_SEGS 14
#define ISLAND_DROP 30.0f /* how far the rim sinks below sea level */
#define TWO_PI 6.2831853f

typedef struct {
    float cx, cz;
    float radius;
    float amp;
    float phase; /* coast wobble offset */
    unsigned int seed;
} Island;

/* cx, cz, radius, amp */
static const float ISLAND_POS[TERRAIN_ISLAND_COUNT][4] = {
    { 0.0f, 78.0f, 52.0f, 18.0f },   /* main island, ahead of the start */
    { 74.0f, 134.0f, 30.0f, 12.0f }, /* east islet */
    { -66.0f, 126.0f, 36.0f, 15.0f },/* west islet */
    { 20.0f, -74.0f, 26.0f, 10.0f }, /* islet behind the start */
};

static Island s_islands[TERRAIN_ISLAND_COUNT];
/* One mesh per island so a whole island can be frustum-culled as a unit. */
static Mesh s_meshes[TERRAIN_ISLAND_COUNT];
/* World AABB per island: x0 y0 z0 x1 y1 z1 (bounds are analytic, see below). */
static float s_bounds[TERRAIN_ISLAND_COUNT][6];
static int s_drawn = 0;
static int s_total = 0;
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

/*
 * Continuous island height for any distance from the centre: above sea level
 * inside the wavy coast, dropping smoothly past the rim. Never jumps, so mesh
 * vertices just outside the coast sink away instead of spiking.
 */
static float island_height_raw(const Island* is, float x, float z) {
    float dx = x - is->cx;
    float dz = z - is->cz;
    float dist = sqrtf(dx * dx + dz * dz);
    float ang = atan2f(dz, dx);

    /* Wavy coast so islands are not perfect circles. */
    float wob = 1.0f + 0.16f * sinf(ang * 3.0f + is->phase) +
                0.09f * sinf(ang * 5.0f + is->phase * 1.7f);
    float r = dist / (is->radius * wob);

    float n = fbm(x * 0.045f, z * 0.045f, is->seed);
    float n2 = fbm(x * 0.11f + 11.0f, z * 0.11f - 7.0f, is->seed + 31u);
    float bump = n * 0.7f + n2 * 0.3f;

    if (r <= 1.0f) {
        float fall = 1.0f - r * r * r; /* broad plateau, steep shore */
        float base = fall * is->amp * (0.22f + 0.9f * bump);
        float shore = (1.0f - fall) * -ISLAND_DROP;
        return base + shore;
    }
    return -(ISLAND_DROP + (r - 1.0f) * ISLAND_DROP);
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

    for (int i = 0; i < TERRAIN_ISLAND_COUNT; i++) {
        s_islands[i].cx = ISLAND_POS[i][0];
        s_islands[i].cz = ISLAND_POS[i][1];
        s_islands[i].radius = ISLAND_POS[i][2];
        s_islands[i].amp = ISLAND_POS[i][3];
        s_islands[i].seed = seed + (unsigned int)i * 997u;
        s_islands[i].phase = (float)(hash_u32(s_islands[i].seed) % 628u) * 0.01f;
    }

    for (int i = 0; i < TERRAIN_ISLAND_COUNT; i++) {
        const Island* is = &s_islands[i];

        mesh_make(&s_meshes[i]);

        /* Analytic AABB: ring vertices sit exactly on the radius circle (the
         * wavy coast only changes height, never XZ), and island_height_raw at
         * those points stays within [-40, amp*1.12]. The margins keep it
         * conservative so culling can never pop an island that is on screen. */
        s_bounds[i][0] = is->cx - is->radius;
        s_bounds[i][1] = -41.0f;
        s_bounds[i][2] = is->cz - is->radius;
        s_bounds[i][3] = is->cx + is->radius;
        s_bounds[i][4] = is->amp * 1.2f;
        s_bounds[i][5] = is->cz + is->radius;
        float pos[TERRAIN_RINGS][TERRAIN_SEGS][3];
        float hgt[TERRAIN_RINGS][TERRAIN_SEGS];

        for (int k = 0; k < TERRAIN_RINGS; k++) {
            float rr = is->radius * ((float)(k + 1) / (float)TERRAIN_RINGS);
            for (int j = 0; j < TERRAIN_SEGS; j++) {
                float a = TWO_PI * (float)j / (float)TERRAIN_SEGS;
                float x = is->cx + cosf(a) * rr;
                float z = is->cz + sinf(a) * rr;
                pos[k][j][0] = x;
                pos[k][j][1] = island_height_raw(is, x, z);
                pos[k][j][2] = z;
                hgt[k][j] = pos[k][j][1];
            }
        }

        /* Summit fan. (center, p(j1), p(j)) winds +Y. */
        float ch = island_height_raw(is, is->cx, is->cz);
        const float center[3] = { is->cx, ch, is->cz };
        for (int j = 0; j < TERRAIN_SEGS; j++) {
            int j1 = (j + 1) % TERRAIN_SEGS;
            float color[4];
            float hc = (ch + hgt[0][j] + hgt[0][j1]) / 3.0f;
            float mx = (is->cx + pos[0][j][0] + pos[0][j1][0]) / 3.0f;
            float mz = (is->cz + pos[0][j][2] + pos[0][j1][2]) / 3.0f;
            terrain_color(hc, mx, mz, is->seed, color);
            mesh_add_triangle(&s_meshes[i], center, pos[0][j1], pos[0][j], color);
        }

        /* Bands between rings; (inner j, inner j1, outer j1, outer j) winds +Y. */
        for (int k = 1; k < TERRAIN_RINGS; k++) {
            for (int j = 0; j < TERRAIN_SEGS; j++) {
                int j1 = (j + 1) % TERRAIN_SEGS;
                float color[4];
                float hc = (hgt[k - 1][j] + hgt[k - 1][j1] + hgt[k][j1] + hgt[k][j]) * 0.25f;
                float mx = (pos[k - 1][j][0] + pos[k - 1][j1][0] + pos[k][j1][0] +
                            pos[k][j][0]) * 0.25f;
                float mz = (pos[k - 1][j][2] + pos[k - 1][j1][2] + pos[k][j1][2] +
                            pos[k][j][2]) * 0.25f;
                terrain_color(hc, mx, mz, is->seed, color);
                mesh_add_quad(&s_meshes[i], pos[k - 1][j], pos[k - 1][j1],
                              pos[k][j1], pos[k][j], color);
            }
        }
    }

    s_built = 1;
}

void terrain_shutdown(void) {
    for (int i = 0; i < TERRAIN_ISLAND_COUNT; i++)
        mesh_destroy(&s_meshes[i]);
    s_built = 0;
}

float terrain_height_at(float x, float z) {
    if (!s_built)
        return -1000.0f;

    float best = -1000.0f;
    for (int i = 0; i < TERRAIN_ISLAND_COUNT; i++) {
        const Island* is = &s_islands[i];
        float dx = x - is->cx;
        float dz = z - is->cz;
        if (dx * dx + dz * dz > is->radius * is->radius)
            continue;
        float h = island_height_raw(is, x, z);
        if (h > best)
            best = h;
    }
    return best;
}

void terrain_normal_at(float x, float z, vec3 out) {
    const float e = 1.5f;
    float hl = terrain_height_at(x - e, z);
    float hr = terrain_height_at(x + e, z);
    float hd = terrain_height_at(x, z - e);
    float hu = terrain_height_at(x, z + e);

    /* Off-map sample: fall back to the local height so the normal stays valid. */
    float h = terrain_height_at(x, z);
    if (hl < -500.0f) hl = h;
    if (hr < -500.0f) hr = h;
    if (hd < -500.0f) hd = h;
    if (hu < -500.0f) hu = h;

    out[0] = hl - hr;
    out[1] = 2.0f * e;
    out[2] = hd - hu;
    glm_vec3_normalize(out);
}

void terrain_emit(const float eye[3]) {
    if (!s_built)
        return;

    static const float identity[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    static const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    mat4 proj, view, clip;
    Plane planes[6];
    memcpy(proj, r_frame()->proj, sizeof(proj));
    memcpy(view, r_frame()->view, sizeof(view));
    glm_mat4_mul(proj, view, clip);
    frustum_extract((const float*)clip, planes);

    r_pass(R_PASS_TERRAIN);
    s_total = TERRAIN_ISLAND_COUNT;
    s_drawn = 0;

    for (int i = 0; i < TERRAIN_ISLAND_COUNT; i++) {
        const float* b = s_bounds[i];

        /* Beyond FOG_END the island is exactly fog colour (== sky), so it is
         * invisible; reject on distance before the plane tests. */
        float dx = 0.0f, dy = 0.0f, dz = 0.0f;
        if (eye[0] < b[0]) dx = b[0] - eye[0];
        else if (eye[0] > b[3]) dx = eye[0] - b[3];
        if (eye[1] < b[1]) dy = b[1] - eye[1];
        else if (eye[1] > b[4]) dy = eye[1] - b[4];
        if (eye[2] < b[2]) dz = b[2] - eye[2];
        else if (eye[2] > b[5]) dz = eye[2] - b[5];
        if (dx * dx + dy * dy + dz * dz > FOG_END * FOG_END)
            continue;

        if (!frustum_aabb_visible(planes, b[0], b[1], b[2], b[3], b[4], b[5]))
            continue;

        s_drawn++;
        r_mesh(&s_meshes[i], identity, white);
    }
}

void terrain_stats(int* drawn, int* total) {
    if (drawn)
        *drawn = s_drawn;
    if (total)
        *total = s_total;
}
