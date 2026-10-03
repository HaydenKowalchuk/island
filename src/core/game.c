#include "game.h"

#include "camera.h"
#include "input.h"
#include "render.h"

/*
 * Core game state and rules. This file must stay platform-agnostic: it may only
 * touch the RenderAPI (render.h) and the input contract (input.h), never GL/GLFW.
 */

static FlyCam s_cam;
static int s_view_w = 1280;
static int s_view_h = 720;

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

static void emit_line(float ax, float ay, float az, float bx, float by, float bz,
                      float r, float g, float b) {
    int base = r_vertex(ax, ay, az, r, g, b, 1.0f, 0.0f, 0.0f);
    r_vertex(bx, by, bz, r, g, b, 1.0f, 0.0f, 0.0f);

    RDraw d = { 0 };
    d.prim = R_LINES;
    d.first = base;
    d.count = 2;
    d.fog = 1;
    d.lit = 0; /* debug world draws flat vertex colours, as it always has */
    d.depth_write = 1;
    d.cull = R_CULL_BACK;
    d.tint[0] = d.tint[1] = d.tint[2] = d.tint[3] = 1.0f;
    r_draw(&d);
}

/*
 * Temporary reference grid and origin gizmo, drawn until the island mesh lands
 * (island1-xf8m). It keeps the camera, fog and lighting verifiable.
 */
static void emit_debug_world(void) {
    const float half = 20.0f;
    const float step = 1.0f;
    const float g = 0.35f, gr = 0.38f, gb = 0.34f;

    for (float x = -half; x <= half; x += step) {
        if (x == 0.0f) continue;
        emit_line(x, 0.0f, -half, x, 0.0f, half, g, gr, gb);
    }
    for (float z = -half; z <= half; z += step) {
        if (z == 0.0f) continue;
        emit_line(-half, 0.0f, z, half, 0.0f, z, g, gr, gb);
    }

    const float len = 4.0f;
    emit_line(0.0f, 0.0f, 0.0f, len, 0.0f, 0.0f, 1.0f, 0.2f, 0.2f);
    emit_line(0.0f, 0.0f, 0.0f, 0.0f, len, 0.0f, 0.2f, 1.0f, 0.2f);
    emit_line(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, len, 0.2f, 0.2f, 1.0f);
}

void game_render(float alpha) {
    (void)alpha; /* interpolated rendering arrives with island1-fnss. */

    r_frame_begin();

    const float fog_color[4] = { SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f };
    r_fog(1, fog_color, FOG_START, FOG_END);

    cam_update(&s_cam, s_view_w, s_view_h);
    r_camera((const float*)s_cam.view, (const float*)s_cam.proj);

    emit_sky();
    emit_debug_world();
}
