#include "game.h"

#include <math.h>
#include <stdio.h>

#include <cglm/cglm.h>

#include "camera.h"
#include "chase_cam.h"
#include "input.h"
#include "mesh.h"
#include "render.h"
#include "texture.h"

/*
 * Core game state and rules. This file must stay platform-agnostic: it may only
 * touch the RenderAPI (render.h) and the input contract (input.h), never GL/GLFW.
 */

static FlyCam s_cam;
static int s_view_w = 1280;
static int s_view_h = 720;

/* Sim time, advanced on the fixed tick and read by the renderer for waves/bob. */
static float s_time = 0.0f;

/* The hover car, built once and drawn with a per-frame transform. */
static Mesh s_car;

/* A textured parcel, bobbing ahead of the car to exercise the texture path. */
static Mesh s_parcel;

/*
 * Chase camera plus a minimal hover-car state it follows. This driving model is
 * a stand-in for island1-9n0n (real physics) and island1-9gio (bob/bank); it
 * exists so the camera has something to track and the feel can be tuned.
 */
static ChaseCam s_chase;

typedef struct {
    vec3 pos;      /* world position; y = water level */
    float heading; /* radians; forward = (sin(h), 0, cos(h)) */
    float speed;   /* units/s (signed) */
    float speed_kmh;
} CarState;

static CarState s_veh;

typedef enum { CAM_CHASE, CAM_FLY } CamMode;
static CamMode s_cam_mode = CAM_CHASE;

static void build_car(void) {
    const float hull[4]  = { 0.42f, 0.06f, 0.07f, 1.0f };
    const float body[4]  = { 0.72f, 0.11f, 0.10f, 1.0f };
    const float glass[4] = { 0.06f, 0.08f, 0.12f, 1.0f };

    mesh_make(&s_car);
    mesh_add_box(&s_car, (const float[]){ 0.0f, 0.34f, 0.0f },
                 (const float[]){ 0.70f, 0.16f, 1.75f }, hull);
    mesh_add_box(&s_car, (const float[]){ 0.0f, 0.60f, 0.0f },
                 (const float[]){ 0.90f, 0.20f, 2.00f }, body);
    mesh_add_box(&s_car, (const float[]){ 0.0f, 0.94f, -0.20f },
                 (const float[]){ 0.60f, 0.22f, 0.80f }, glass);
    mesh_add_box(&s_car, (const float[]){ 0.0f, 0.58f, 1.62f },
                 (const float[]){ 0.55f, 0.12f, 0.38f }, body);
}

/* White cube textured with the parcel tile, so the texture path is visible. */
static void build_parcel(void) {
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    mesh_make(&s_parcel);
    mesh_add_box(&s_parcel, (const float[]){ 0.0f, 0.0f, 0.0f },
                 (const float[]){ 0.35f, 0.35f, 0.35f }, white);
    s_parcel.texture = texture_get("textures/parcel_32.png");
}

void game_init(void) {
    cam_init(&s_cam);

    glm_vec3_zero(s_veh.pos);
    s_veh.heading = 0.0f;
    s_veh.speed = 0.0f;
    s_veh.speed_kmh = 0.0f;
    chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
    s_cam_mode = CAM_CHASE;

    build_car();
    build_parcel();
}

void game_shutdown(void) {
    mesh_destroy(&s_car);
    mesh_destroy(&s_parcel);
}

void game_resize(int width, int height) {
    if (width > 0 && height > 0) {
        s_view_w = width;
        s_view_h = height;
    }
}

/* Minimal arcade driving; replaced by island1-9n0n later. */
#define CAR_ACCEL     9.0f
#define CAR_DRAG      0.9f
#define CAR_MAX_SPEED 30.0f
#define CAR_REVERSE   8.0f
#define CAR_TURN_RATE 1.9f

static void drive_car(float dt) {
    float throttle = input_axis_throttle();
    float steer = input_axis_steer();

    s_veh.speed += throttle * CAR_ACCEL * dt;
    s_veh.speed -= s_veh.speed * CAR_DRAG * dt;

    if (s_veh.speed > CAR_MAX_SPEED) s_veh.speed = CAR_MAX_SPEED;
    if (s_veh.speed < -CAR_REVERSE) s_veh.speed = -CAR_REVERSE;

    /* Steering bites harder with speed, so the car feels planted when still. */
    float speed01 = fabsf(s_veh.speed) / CAR_MAX_SPEED;
    s_veh.heading += steer * CAR_TURN_RATE * dt * (0.35f + 0.65f * speed01);

    s_veh.pos[0] += sinf(s_veh.heading) * s_veh.speed * dt;
    s_veh.pos[2] += cosf(s_veh.heading) * s_veh.speed * dt;
    s_veh.speed_kmh = fabsf(s_veh.speed) * 3.6f;

    if (input_pressed(ACT_RESET)) {
        glm_vec3_zero(s_veh.pos);
        s_veh.heading = 0.0f;
        s_veh.speed = 0.0f;
        s_veh.speed_kmh = 0.0f;
        chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
    }
}

void game_tick(float dt) {
    const InputState* in = input_state();

    s_time += dt;

    if (input_pressed(ACT_TOGGLE_CAM)) {
        s_cam_mode = (s_cam_mode == CAM_CHASE) ? CAM_FLY : CAM_CHASE;
        if (s_cam_mode == CAM_FLY)
            cam_reset(&s_cam);
        else
            chase_cam_init(&s_chase, s_veh.pos, s_veh.heading);
    }

    if (s_cam_mode == CAM_FLY) {
        /* Free-fly dev camera: look and speed adjustments stay in the sim. */
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
    } else {
        drive_car(dt);
    }
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
    const float speed01 = fabsf(s_veh.speed) / CAR_MAX_SPEED;
    const float bank = -input_axis_steer() * 0.08f * speed01;
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    mat4 world;
    glm_mat4_identity(world);
    glm_translate(world, (vec3){ s_veh.pos[0], s_veh.pos[1] + bob, s_veh.pos[2] });
    glm_rotate(world, s_veh.heading, (vec3){ 0.0f, 1.0f, 0.0f });
    glm_rotate(world, bank, (vec3){ 0.0f, 0.0f, 1.0f });

    r_mesh(&s_car, (const float*)world, white);
}

/* A textured parcel spinning slightly ahead of the car. */
static void emit_parcel(float t) {
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float fwd[3] = { sinf(s_veh.heading), 0.0f, cosf(s_veh.heading) };

    mat4 world;
    glm_mat4_identity(world);
    glm_translate(world, (vec3){
        s_veh.pos[0] + fwd[0] * 5.0f,
        1.5f + 0.25f * sinf(t * 2.0f),
        s_veh.pos[2] + fwd[2] * 5.0f,
    });
    glm_rotate(world, t * 0.8f, (vec3){ 0.0f, 1.0f, 0.0f });

    r_mesh(&s_parcel, (const float*)world, white);
}

/* Foam wake trailing the car, laid flat on the water surface. */
#define WAKE_POINTS 14

static void emit_wake(float t) {
    float pts[WAKE_POINTS * 3];
    float fx = sinf(s_veh.heading);
    float fz = cosf(s_veh.heading);
    float rx = fz;  /* right vector: forward rotated about Y */
    float rz = -fx;

    for (int i = 0; i < WAKE_POINTS; i++) {
        float s = (float)i;
        float back = 1.6f + s * 1.6f;
        float sway = 0.9f * sinf(s * 0.5f + t * 1.5f) * (s / (float)(WAKE_POINTS - 1));
        float x = s_veh.pos[0] - fx * back + rx * sway;
        float z = s_veh.pos[2] - fz * back + rz * sway;
        pts[i * 3 + 0] = x;
        pts[i * 3 + 1] = wave_height(x, z, t) + 0.03f;
        pts[i * 3 + 2] = z;
    }

    const float foam[4] = { 0.90f, 0.97f, 1.0f, 0.55f };
    r_ribbon(pts, WAKE_POINTS, 1.6f, foam);
}

void game_render(float frame_dt) {
    r_frame_begin();
    r_viewport(s_view_w, s_view_h);

    const float fog_color[4] = { SKY_COLOR_R, SKY_COLOR_G, SKY_COLOR_B, 1.0f };
    r_fog(1, fog_color, FOG_START, FOG_END);

    if (s_cam_mode == CAM_CHASE) {
        chase_cam_update(&s_chase, s_veh.pos, s_veh.heading, s_veh.speed_kmh,
                         s_view_w, s_view_h, frame_dt);
        r_camera((const float*)s_chase.view, (const float*)s_chase.proj);
    } else {
        cam_update(&s_cam, s_view_w, s_view_h);
        r_camera((const float*)s_cam.view, (const float*)s_cam.proj);
    }

    emit_sky();
    emit_ocean(s_time);
    emit_wake(s_time);
    emit_car(s_time);
    emit_parcel(s_time);

    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    char buf[32];
    snprintf(buf, sizeof(buf), "%d KMH", (int)(s_veh.speed_kmh + 0.5f));

    r_text_2d(12, 10, 2.0f, s_cam_mode == CAM_CHASE ? "CHASE" : "FLY", white);
    r_text_2d(12, 32, 1.5f, buf, white);
    r_text_2d(12, 54, 1.0f, "WASD DRIVE  F FLY  R RESET", white);
}
