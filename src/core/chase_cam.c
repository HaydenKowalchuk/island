#include "chase_cam.h"

#include <math.h>

#include "pi.h"

#define CHASE_W         8.0f   /* spring angular frequency (critical damping) */
#define CHASE_LAG_RATE  6.0f   /* heading ease rate, 1/s */
#define CHASE_FOV_BASE  58.0f
#define CHASE_FOV_GAIN  8.0f
#define CHASE_ZNEAR     0.1f
#define CHASE_ZFAR      1000.0f

/*
 * Keep the horizontal field of view constant across display aspects so a 4:3
 * Dreamcast (640x480) sees the same width as a 16:9 desktop/PSP (1280x720,
 * 480x272). FOV values below are referenced to 16:9; the effective vertical FOV
 * is derived from the real aspect each frame (desktop stays identical).
 */
#define CHASE_REF_ASPECT (16.0f / 9.0f)

/* A frame hiccup must not blow up the spring; clamp the integration step. */
#define CHASE_MAX_DT    (1.0f / 30.0f)

static ChaseGroundFn s_ground = NULL;

/* Vehicle framing by default; island1-zku4 switches these on foot. */
static float s_base_dist = 7.0f;
static float s_base_height = 3.2f;
static float s_look_height = 0.8f;

void chase_cam_set_ground(ChaseGroundFn fn) {
    s_ground = fn;
}

void chase_cam_set_framing(float dist, float height, float look_height) {
    s_base_dist = dist;
    s_base_height = height;
    s_look_height = look_height;
}

static float angle_wrap(float a) {
    while (a > (float)ISLAND_PI)
        a -= 2.0f * (float)ISLAND_PI;
    while (a < -(float)ISLAND_PI)
        a += 2.0f * (float)ISLAND_PI;
    return a;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Vertical FOV (radians) that preserves the 16:9 horizontal FOV at `aspect`. */
static float vfov_for_aspect(float ref_vfov_deg, float aspect) {
    float half_h = tanf(ref_vfov_deg * (float)(ISLAND_PI / 180.0) * 0.5f) *
                   CHASE_REF_ASPECT;
    return 2.0f * atanf(half_h / aspect);
}

static void place_behind(ChaseCam* c, const float car_pos[3], float heading) {
    float fwd[3] = { sinf(heading), 0.0f, cosf(heading) };

    c->pos[0] = car_pos[0] - fwd[0] * s_base_dist;
    c->pos[1] = car_pos[1] + s_base_height;
    c->pos[2] = car_pos[2] - fwd[2] * s_base_dist;
    glm_vec3_zero(c->vel);
}

void chase_cam_init(ChaseCam* c, const float car_pos[3], float heading) {
    c->heading_lag = heading;
    c->fov = CHASE_FOV_BASE;
    place_behind(c, car_pos, heading);
    glm_mat4_identity(c->view);
    glm_mat4_identity(c->proj);
}

void chase_cam_update(ChaseCam* c, const float car_pos[3], float heading,
                      float speed_kmh, int width, int height, float dt) {
    if (dt <= 0.0f)
        dt = CHASE_MAX_DT;
    if (dt > CHASE_MAX_DT)
        dt = CHASE_MAX_DT;

    /* Ease the heading; a hard turn shows visible lag that then settles. */
    float dh = angle_wrap(heading - c->heading_lag);
    c->heading_lag += dh * (1.0f - expf(-CHASE_LAG_RATE * dt));

    float s01 = clampf(speed_kmh / 100.0f, 0.0f, 1.0f);

    /* Pull back and rise as the target speeds up. */
    float dist = s_base_dist + s01 * 4.0f;
    float cam_height = s_base_height + s01 * 1.2f;

    float fwd[3] = { sinf(c->heading_lag), 0.0f, cosf(c->heading_lag) };

    vec3 target = {
        car_pos[0] - fwd[0] * dist,
        car_pos[1] + cam_height,
        car_pos[2] - fwd[2] * dist,
    };

    /* Critically-damped spring: acc = (target - pos) * w^2 - 2w * vel. */
    for (int i = 0; i < 3; i++) {
        float acc = (target[i] - c->pos[i]) * CHASE_W * CHASE_W - 2.0f * CHASE_W * c->vel[i];
        c->vel[i] += acc * dt;
        c->pos[i] += c->vel[i] * dt;
    }

    /* Keep the camera out of hills (island1-qvn1). */
    if (s_ground) {
        float g = s_ground(c->pos[0], c->pos[2]);
        if (g > -500.0f && c->pos[1] < g + 1.5f)
            c->pos[1] = g + 1.5f;
    }

    vec3 look = {
        car_pos[0] + fwd[0] * 4.0f,
        car_pos[1] + s_look_height,
        car_pos[2] + fwd[2] * 4.0f,
    };

    glm_lookat(c->pos, look, (vec3){ 0.0f, 1.0f, 0.0f }, c->view);

    c->fov = CHASE_FOV_BASE + s01 * CHASE_FOV_GAIN; /* reference FOV at 16:9 */
    float aspect =
        (height > 0) ? (float)width / (float)height : CHASE_REF_ASPECT;
    glm_perspective(vfov_for_aspect(c->fov, aspect), aspect, CHASE_ZNEAR,
                    CHASE_ZFAR, c->proj);
}
