#include "game.h"

#include <math.h>

#include "camera.h"
#include "input.h"
#include "mesh.h"
#include "render.h"

/*
 * Core game state and rules. This file must stay platform-agnostic: it may only
 * touch the RenderAPI (render.h) and the input contract (input.h), never GL/GLFW.
 */

static FlyCam s_cam;
static int s_view_w = 1280;
static int s_view_h = 720;

/* Sim time, advanced on the fixed tick and read by the renderer for waves/bob. */
static float s_time = 0.0f;

void game_init(void) {
    cam_init(&s_cam);
}

void game_resize(int width, int height) {
    if (width > 0 && height > 0) {
        s_view_w = width;
        s_view_h = height;
    }
}

void game_tick(float dt) {
    const InputState* in = input_state();

    s_time += dt;

    /* Look and speed adjustments belong to the sim so they stay in step. */
    if (input_held(ACT_LOOK))
        cam_rotate(&s_cam, in->mouse_dx, in->mouse_dy);

    if (input_held(ACT_BOOST))
        s_cam.speed = 40.0f;
    else if (input_held(ACT_SLOW))
        s_cam.speed = 3.0f;
    else
        s_cam.speed = 10.0f;

    if (input_pressed(ACT_RESET))
        cam_reset(&s_cam);

    cam_move(&s_cam, (int)input_axis_throttle(), (int)input_axis_steer(), 0, dt);
}

/* ---- Frame recording -------------------------------------------------- */

/* Sky: a normalised window-space quad with a vertical gradient. */
static void emit_sky(void) {
    int base = r_vertex(0.0f, 0.0f, 0.0f, SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f, 0.0f, 0.0f);
    r_vertex(1.0f, 0.0f, 0.0f, SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f, 0.0f, 0.0f);
    r_vertex(1.0f, 1.0f, 0.0f, SKY_ZENITH_R, SKY_ZENITH_G, SKY_ZENITH_B, 1.0f, 1.0f, 0.0f);
    r_vertex(0.0f, 1.0f, 0.0f, SKY_ZENITH_R, SKY_ZENITH_G, SKY_ZENITH_B, 1.0f, 1.0f, 0.0f);

    RDraw d = { 0 };
    d.prim = R_QUADS;
    d.first = base;
    d.count = 4;
    d.screen_space = 1;
    d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
    r_draw(&d);
}

/* ---- Ocean ------------------------------------------------------------ */

#define OCEAN_N 40
#define OCEAN_SIZE 400.0f
#define OCEAN_WAVE_AMP 0.16f

static float hash2(int i, int j) {
    float n = sinf((float)i * 127.1f + (float)j * 311.7f) * 43758.5453f;
    return n - floorf(n); /* 0..1 */
}

static float wave_height(float x, float z, float t) {
    return OCEAN_WAVE_AMP * sinf(x * 0.12f + t * 1.1f)
         + 0.10f * cosf(z * 0.17f - t * 0.8f);
}

/*
 * One batched draw of a subdivided water plane. Each cell is flat-shaded (its
 * four corners share a colour) which gives the faceted PS1 read; the vertex
 * heights come from a smooth function of world x/z, so neighbouring cells still
 * line up without cracks.
 */
static void emit_ocean(float t) {
    const float step = OCEAN_SIZE / (float)OCEAN_N;
    const float half = OCEAN_SIZE * 0.5f;

    int first = -1;
    int count = 0;

    for (int i = 0; i < OCEAN_N; i++) {
        float z0 = -half + (float)i * step;
        float z1 = z0 + step;

        for (int j = 0; j < OCEAN_N; j++) {
            float x0 = -half + (float)j * step;
            float x1 = x0 + step;

            float h = hash2(i * 3, j * 7);
            float r = 0.05f + 0.10f * h;
            float g = 0.30f + 0.22f * h;
            float b = 0.42f + 0.20f * h;

            float y00 = wave_height(x0, z0, t);
            float y10 = wave_height(x1, z0, t);
            float y11 = wave_height(x1, z1, t);
            float y01 = wave_height(x0, z1, t);

            int v = r_vertex(x0, y00, z0, r, g, b, 1.0f, 0.0f, 0.0f);
            if (first < 0)
                first = v;
            r_vertex(x1, y10, z0, r, g, b, 1.0f, 0.0f, 0.0f);
            r_vertex(x1, y11, z1, r, g, b, 1.0f, 0.0f, 0.0f);
            r_vertex(x0, y01, z1, r, g, b, 1.0f, 0.0f, 0.0f);
            count += 4;
        }
    }

    RDraw d = { 0 };
    d.prim = R_QUADS;
    d.first = first;
    d.count = count;
    d.fog = 1;
    d.depth_write = 1;
    d.cull = R_CULL_NONE;
    d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
    r_draw(&d);
}

/* ---- Hover car -------------------------------------------------------- */

/*
 * A red car/boat hybrid as a handful of boxes (no wheels, per the art brief):
 * a dark under-hull, a red body, a dark wedge cabin and a low nose. It bobs and
 * rocks gently so it reads as floating on the water.
 */
static void emit_car(float t) {
    const float bob = 0.05f * sinf(t * 1.6f);
    const float roll = 0.04f * sinf(t * 1.1f + 1.0f);

    const float hull[4]  = { 0.42f, 0.06f, 0.07f, 1.0f };
    const float body[4]  = { 0.72f, 0.11f, 0.10f, 1.0f };
    const float glass[4] = { 0.06f, 0.08f, 0.12f, 1.0f };

    float y = 0.34f + bob;

    mesh_box((const float[]){ 0.0f, y, 0.0f },
             (const float[]){ 0.70f, 0.16f, 1.75f }, hull, 1, 0);

    /* Body sits slightly rotated by offsetting the top to fake the roll. */
    mesh_box((const float[]){ roll * 1.5f, y + 0.26f, 0.0f },
             (const float[]){ 0.90f, 0.20f, 2.00f }, body, 1, 0);

    mesh_box((const float[]){ roll * 2.0f, y + 0.60f, -0.20f },
             (const float[]){ 0.60f, 0.22f, 0.80f }, glass, 1, 0);

    mesh_box((const float[]){ 0.0f, y + 0.24f, 1.62f },
             (const float[]){ 0.55f, 0.12f, 0.38f }, body, 1, 0);
}

void game_render(float alpha) {
    (void)alpha; /* interpolated rendering arrives with island1-fnss. */

    r_frame_begin();

    const float fog_color[4] = { SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f };
    r_fog(1, fog_color, FOG_START, FOG_END);

    cam_update(&s_cam, s_view_w, s_view_h);
    r_camera((const float*)s_cam.view, (const float*)s_cam.proj);

    emit_sky();
    emit_ocean(s_time);
    emit_car(s_time);
}
